#include "resources/source_artwork_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <variant>

#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

using ::testing::HasSubstr;

RgbaImage TestImage() {
  return RgbaImage{
      .width = 2,
      .height = 2,
      .pixels = {10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 0, 0, 0, 0},
  };
}

class SourceArtworkManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            ("zebes-source-artwork-" + std::to_string(++sequence));
    std::filesystem::remove_all(path_);
    ASSERT_OK_AND_ASSIGN(manager_, SourceArtworkManager::Create(path_.string()));
    ASSERT_OK(manager_->LoadAllArtwork());
  }

  void TearDown() override { std::filesystem::remove_all(path_); }

  std::filesystem::path path_;
  std::unique_ptr<SourceArtworkManager> manager_;
  static int sequence;
};

int SourceArtworkManagerTest::sequence = 0;

TEST_F(SourceArtworkManagerTest, CreatesAndReloadsAnImportedSourceFromCanonicalPixels) {
  const RgbaImage image = TestImage();
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              image));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<SourceArtworkManager> reloaded,
                       SourceArtworkManager::Create(path_.string()));
  ASSERT_OK(reloaded->LoadAllArtwork());
  ASSERT_OK_AND_ASSIGN(SourceArtwork * artwork, reloaded->GetArtwork(id));
  EXPECT_EQ(artwork->source_path, "source_art/" + id + ".png");
  EXPECT_EQ(artwork->width, image.width);
  EXPECT_EQ(artwork->height, image.height);
  EXPECT_EQ(artwork->content_digest.size(), 64u);
  ASSERT_TRUE(std::holds_alternative<ImportedArtworkProvenance>(artwork->provenance));

  ASSERT_OK_AND_ASSIGN(const RgbaImage decoded, reloaded->ReadArtworkPixels(id));
  EXPECT_EQ(decoded.pixels, image.pixels);
}

TEST_F(SourceArtworkManagerTest, GeneratedProvenanceWritesExplicitNulls) {
  ASSERT_OK_AND_ASSIGN(const std::string id,
                       manager_->CreateArtwork("Generated tree",
                                               GeneratedArtworkProvenance{
                                                   .provider = "provider",
                                                   .model = "model-v1",
                                                   .submitted_prompt = "one tree",
                                                   .generated_at_utc = "2026-08-16T15:04:05Z",
                                               },
                                               TestImage()));

  std::ifstream stream(path_ / "definitions/source_artworks" / (id + ".json"));
  nlohmann::json json;
  stream >> json;
  EXPECT_TRUE(json.at("provenance").at("revised_prompt").is_null());
  EXPECT_TRUE(json.at("provenance").at("provider_request_id").is_null());
}

TEST_F(SourceArtworkManagerTest, RejectsPixelsChangedAfterAcceptance) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  const RgbaImage replacement{
      .width = 2,
      .height = 2,
      .pixels = {1, 2, 3, 255, 1, 2, 3, 255, 1, 2, 3, 255, 1, 2, 3, 255},
  };
  ASSERT_OK(WritePng((path_ / "source_art" / (id + ".png")).string(), replacement.width,
                     replacement.height, replacement.pixels));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<SourceArtworkManager> reloaded,
                       SourceArtworkManager::Create(path_.string()));
  const absl::Status status = reloaded->LoadAllArtwork();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(status.message()), HasSubstr("digest"));
}

TEST_F(SourceArtworkManagerTest, RejectsAPathOutsideTheIdBackedSourceDirectory) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  const std::filesystem::path definition = path_ / "definitions/source_artworks" / (id + ".json");
  std::ifstream input(definition);
  nlohmann::json json;
  input >> json;
  input.close();
  json["source_path"] = "../outside.png";
  std::ofstream(definition, std::ios::trunc) << json.dump(2);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<SourceArtworkManager> reloaded,
                       SourceArtworkManager::Create(path_.string()));
  const absl::Status status = reloaded->LoadAllArtwork();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(status.message()), HasSubstr("ID-backed"));
}

TEST_F(SourceArtworkManagerTest, EnforcesLimitsBeforeWritingAnything) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<SourceArtworkManager> limited,
      SourceArtworkManager::Create(path_.string(), SourceArtworkLimits{.maximum_width = 1}));
  const absl::Status status =
      limited
          ->CreateArtwork("Too large",
                          ImportedArtworkProvenance{.original_filename = "oak.png",
                                                    .imported_at_utc = "2026-08-16T15:04:05Z"},
                          TestImage())
          .status();
  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
  EXPECT_TRUE(std::filesystem::is_empty(path_ / "definitions/source_artworks"));
}

TEST_F(SourceArtworkManagerTest, DeleteRemovesDefinitionAndRetainedPixels) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  ASSERT_OK(manager_->DeleteArtwork(id));

  EXPECT_FALSE(std::filesystem::exists(path_ / "definitions/source_artworks" / (id + ".json")));
  EXPECT_FALSE(std::filesystem::exists(path_ / "source_art" / (id + ".png")));
  EXPECT_EQ(manager_->GetArtwork(id).status().code(), absl::StatusCode::kNotFound);
}

}  // namespace
}  // namespace zebes
