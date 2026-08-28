#include "curation/review.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "common/utils.h"
#include "curation/candidate_commit.h"
#include "curation/registry.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

RgbaImage Image() {
  return {
      .width = 2,
      .height = 1,
      .pixels = {255, 0, 0, 255, 0, 0, 255, 255},
  };
}

CurationReview MakeReview() {
  return {
      .kind = "test-kind",
      .asset_id = "asset-id",
      .asset_name = "Asset Name",
      .metadata = {{"purpose", "test"}},
      .artifacts = {{
          .id = "main",
          .relative_path = "views/main.png",
          .description = "Main review image",
          .image = Image(),
      }},
      .findings = {{
          .severity = CurationFindingSeverity::kInfo,
          .code = "checked",
          .subject = "Asset Name",
          .message = "reviewed",
      }},
  };
}

class TemporaryReviewDirectory {
 public:
  TemporaryReviewDirectory()
      : path_(std::filesystem::temp_directory_path() /
              std::filesystem::path("zebes-curation-test-" + GenerateGuid())) {}
  ~TemporaryReviewDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  std::filesystem::path path() const { return path_; }

 private:
  std::filesystem::path path_;
};

class TestReviewer : public CurationReviewer {
 public:
  explicit TestReviewer(std::string kind) : kind_(std::move(kind)) {}

  std::string_view kind() const override { return kind_; }

  absl::StatusOr<CurationReview> Review(Api& api,
                                        const CurationReviewRequest& request) const override {
    (void)api;
    CurationReview review = MakeReview();
    review.kind = kind_;
    review.asset_id = request.asset_id;
    return review;
  }

 private:
  std::string kind_;
};

class TestApi : public Api {
 public:
  TestApi() : Api() {}
};

class TestCommitReviewer : public TestReviewer {
 public:
  TestCommitReviewer(bool& committed, absl::Status commit_status)
      : TestReviewer("committable"),
        committed_(committed),
        commit_status_(std::move(commit_status)) {}

  absl::StatusOr<CurationReview> ReviewCandidate(Api& api, const CurationReviewRequest& request,
                                                 const nlohmann::json& candidate) const override {
    (void)candidate;
    return Review(api, request);
  }

  absl::Status CommitCandidate(Api& api, const CurationReviewRequest& request,
                               const nlohmann::json& candidate) const override {
    (void)api;
    (void)request;
    (void)candidate;
    committed_ = true;
    return commit_status_;
  }

 private:
  bool& committed_;
  absl::Status commit_status_;
};

TEST(CurationReviewTest, PublishesImagesAndManifestAsOneNewDirectory) {
  TemporaryReviewDirectory temporary;
  const std::filesystem::path output = temporary.path() / "review";
  ASSERT_OK(PublishCurationReview(MakeReview(), output.string()));

  EXPECT_TRUE(std::filesystem::exists(output / "views/main.png"));
  std::ifstream stream(output / "manifest.json");
  ASSERT_TRUE(stream.is_open());
  const nlohmann::json manifest = nlohmann::json::parse(stream);
  EXPECT_EQ(manifest.at("schema_version"), kCurationReviewSchemaVersion);
  EXPECT_EQ(manifest.at("kind"), "test-kind");
  EXPECT_EQ(manifest.at("artifacts").at(0).at("path"), "views/main.png");
  EXPECT_EQ(manifest.at("artifacts").at(0).at("rgba_sha256").get<std::string>().size(), 64);

  EXPECT_TRUE(absl::IsAlreadyExists(PublishCurationReview(MakeReview(), output.string())));
}

TEST(CurationReviewTest, RejectsTraversalAndDuplicateArtifactsBeforeWriting) {
  CurationReview review = MakeReview();
  review.artifacts.front().relative_path = "../escape.png";
  EXPECT_FALSE(ValidateCurationReview(review).ok());

  review = MakeReview();
  review.artifacts.push_back(review.artifacts.front());
  EXPECT_FALSE(ValidateCurationReview(review).ok());
}

TEST(CurationReviewTest, StreamsArtifactsThroughTheAtomicPublicationBoundary) {
  TemporaryReviewDirectory temporary;
  const std::filesystem::path output = temporary.path() / "streamed";

  ASSERT_OK_AND_ASSIGN(
      const size_t artifact_count,
      PublishCurationReviewStreamed(
          output.string(), [](CurationArtifactSink& sink, CurationReview& review) -> absl::Status {
            review = MakeReview();
            CurationArtifact artifact = std::move(review.artifacts.front());
            review.artifacts.clear();
            return sink.Add(artifact);
          }));

  EXPECT_EQ(artifact_count, 1);
  EXPECT_TRUE(std::filesystem::exists(output / "views/main.png"));
  std::ifstream stream(output / "manifest.json");
  ASSERT_TRUE(stream.is_open());
  const nlohmann::json manifest = nlohmann::json::parse(stream);
  EXPECT_EQ(manifest.at("artifacts").at(0).at("id"), "main");
  EXPECT_EQ(manifest.at("findings").at(0).at("code"), "checked");
}

TEST(CurationReviewTest, RemovesStreamedStagingWhenALaterArtifactIsInvalid) {
  TemporaryReviewDirectory temporary;
  const std::filesystem::path output = temporary.path() / "streamed";

  const absl::Status status =
      PublishCurationReviewStreamed(
          output.string(),
          [](CurationArtifactSink& sink, CurationReview& review) -> absl::Status {
            review = MakeReview();
            CurationArtifact artifact = std::move(review.artifacts.front());
            review.artifacts.clear();
            const absl::Status first = sink.Add(artifact);
            if (!first.ok()) return first;
            return sink.Add(artifact);
          })
          .status();

  EXPECT_TRUE(absl::IsInvalidArgument(status));
  EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(CurationRegistryTest, DispatchesByStableKindAndRejectsDuplicateRegistration) {
  CurationRegistry registry;
  ASSERT_OK(registry.Add(std::make_unique<TestReviewer>("example")));
  EXPECT_TRUE(absl::IsAlreadyExists(registry.Add(std::make_unique<TestReviewer>("example"))));

  TestApi api;
  ASSERT_OK_AND_ASSIGN(CurationReview review,
                       registry.Review(api, "example", {.asset_id = "selected"}));
  EXPECT_EQ(review.kind, "example");
  EXPECT_EQ(review.asset_id, "selected");
  EXPECT_TRUE(absl::IsNotFound(registry.Review(api, "missing", {.asset_id = "selected"}).status()));
  EXPECT_TRUE(absl::IsUnimplemented(
      registry.ReviewCandidate(api, "example", {.asset_id = "selected"}, nlohmann::json::object())
          .status()));

  TemporaryReviewDirectory temporary;
  ASSERT_OK_AND_ASSIGN(const size_t artifact_count,
                       registry.PublishReview(api, "example", {.asset_id = "selected"},
                                              (temporary.path() / "published").string()));
  EXPECT_EQ(artifact_count, 1);
  EXPECT_TRUE(std::filesystem::exists(temporary.path() / "published/manifest.json"));
}

TEST(CurationCandidateCommitTest, LeavesReviewedEvidenceWhenTheTransactionFails) {
  TemporaryReviewDirectory temporary;
  const std::filesystem::path output = temporary.path() / "candidate";
  bool committed = false;
  CurationRegistry registry;
  ASSERT_OK(registry.Add(std::make_unique<TestCommitReviewer>(
      committed, absl::InternalError("injected transaction failure"))));
  TestApi api;

  const absl::Status status =
      CommitCandidateWithEvidence(registry, api, "committable", {.asset_id = "selected"},
                                  nlohmann::json::object(), output.string());

  EXPECT_TRUE(absl::IsInternal(status));
  EXPECT_TRUE(committed);
  EXPECT_TRUE(std::filesystem::exists(output / "manifest.json"));
  EXPECT_FALSE(std::filesystem::exists(CommittedCurationOutputPath(output.string())));
}

TEST(CurationCandidateCommitTest, PublishesPersistedEvidenceAfterACommit) {
  TemporaryReviewDirectory temporary;
  const std::filesystem::path output = temporary.path() / "candidate";
  bool committed = false;
  CurationRegistry registry;
  ASSERT_OK(registry.Add(std::make_unique<TestCommitReviewer>(committed, absl::OkStatus())));
  TestApi api;

  ASSERT_OK(CommitCandidateWithEvidence(registry, api, "committable", {.asset_id = "selected"},
                                        nlohmann::json::object(), output.string()));

  EXPECT_TRUE(committed);
  EXPECT_TRUE(std::filesystem::exists(output / "manifest.json"));
  EXPECT_TRUE(std::filesystem::exists(
      std::filesystem::path(CommittedCurationOutputPath(output.string())) / "manifest.json"));
}

}  // namespace
}  // namespace zebes
