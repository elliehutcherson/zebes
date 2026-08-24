#include "editor/image_generation/image_generation_request_controller.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "editor/image_generation/image_generation.h"
#include "editor/image_generation/image_generation_engine.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

ImageGenerationResult ResultFor(std::string prompt) {
  return ImageGenerationResult{
      .provider = "fake",
      .model = "fake-model",
      .submitted_prompt = std::move(prompt),
      .candidates = {{.image = {.width = 1, .height = 1, .pixels = {1, 2, 3, 255}}}},
  };
}

class PromptOperation final : public ImageGenerationOperation {
 public:
  explicit PromptOperation(std::string prompt) : prompt_(std::move(prompt)) {}

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll() override {
    if (prompt_ == "fail") return absl::UnauthenticatedError("provider login expired");
    return std::optional<ImageGenerationResult>(ResultFor(std::move(prompt_)));
  }

  void Cancel() noexcept override {}

 private:
  std::string prompt_;
};

class PromptClient final : public ImageGenerationClient {
 public:
  ImageGenerationCapabilities Capabilities() const override {
    return ImageGenerationCapabilities{.maximum_candidates = 2};
  }

 protected:
  absl::StatusOr<ImageGenerationRequest> StartValidated(ImageGenerationSpec spec) override {
    return ImageGenerationRequest::Create(
        std::make_unique<PromptOperation>(std::move(spec.prompt)));
  }
};

ImageGenerationSpec SpecFor(std::string prompt) {
  return ImageGenerationSpec{
      .prompt = std::move(prompt),
      .requested_candidates = 1,
      .target_aspect = {.width = 1, .height = 1},
  };
}

TEST(ImageGenerationRequestControllerTest, RejectsMissingRegistry) {
  const absl::Status status = ImageGenerationRequestController::Create(nullptr).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(ImageGenerationRequestControllerTest, RoutesSharedEngineResultsToTheirOwningSurface) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationEngine> engine,
                       ImageGenerationEngine::Create(std::make_unique<PromptClient>()));
  ImageGenerationProviderRegistry registry{
      .providers = {{.name = "Fake", .engine = engine.get()}},
  };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationRequestController> prop,
                       ImageGenerationRequestController::Create(&registry));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationRequestController> parallax,
                       ImageGenerationRequestController::Create(&registry));

  ASSERT_OK(prop->Submit(SpecFor("prop")));
  ASSERT_OK(parallax->Submit(SpecFor("parallax")));
  EXPECT_EQ(prop->Submit(SpecFor("duplicate")).code(), absl::StatusCode::kFailedPrecondition);
  ASSERT_OK(engine->Run().status());

  ASSERT_OK_AND_ASSIGN(const bool parallax_completed, parallax->Poll());
  ASSERT_TRUE(parallax_completed);
  ASSERT_TRUE(parallax->review().has_value());
  EXPECT_EQ(parallax->review()->submitted_prompt, "parallax");
  ASSERT_OK_AND_ASSIGN(const bool prop_completed, prop->Poll());
  ASSERT_TRUE(prop_completed);
  ASSERT_TRUE(prop->review().has_value());
  EXPECT_EQ(prop->review()->submitted_prompt, "prop");
}

TEST(ImageGenerationRequestControllerTest, DisabledProviderRetainsAnotherSurfacesPendingResult) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationEngine> engine,
                       ImageGenerationEngine::Create(std::make_unique<PromptClient>()));
  ImageGenerationProviderRegistry registry{
      .providers = {{.name = "Fake", .engine = engine.get()}},
  };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationRequestController> failing,
                       ImageGenerationRequestController::Create(&registry));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationRequestController> pending,
                       ImageGenerationRequestController::Create(&registry));

  ASSERT_OK(failing->Submit(SpecFor("fail")));
  ASSERT_OK(pending->Submit(SpecFor("still mine")));
  ASSERT_OK(engine->Run().status());

  const absl::StatusOr<bool> failure = failing->Poll();
  EXPECT_EQ(failure.status().code(), absl::StatusCode::kUnauthenticated);
  EXPECT_FALSE(registry.providers[0].available());
  EXPECT_EQ(registry.providers[0].engine, engine.get());

  ASSERT_OK_AND_ASSIGN(const bool completed, pending->Poll());
  ASSERT_TRUE(completed);
  ASSERT_TRUE(pending->review().has_value());
  EXPECT_EQ(pending->review()->submitted_prompt, "still mine");
  EXPECT_EQ(pending->Submit(SpecFor("new request")).code(), absl::StatusCode::kUnavailable);
}

TEST(ImageGenerationRequestControllerTest, OwnsCandidateNavigationAcceptanceAndDiscard) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationEngine> engine,
                       ImageGenerationEngine::Create(std::make_unique<PromptClient>()));
  ImageGenerationProviderRegistry registry{
      .providers = {{.name = "Fake", .engine = engine.get()}},
  };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationRequestController> controller,
                       ImageGenerationRequestController::Create(&registry));
  ASSERT_OK(controller->Submit(SpecFor("review me")));
  ASSERT_OK(engine->Run().status());
  ASSERT_OK_AND_ASSIGN(const bool completed, controller->Poll());
  ASSERT_TRUE(completed);

  controller->SelectCandidate(9);
  EXPECT_EQ(controller->review()->selected, 0);
  const absl::Status refused = controller->AcceptCandidate(
      [](const ImageGenerationReview&, const ImageGenerationCandidate&) {
        return absl::UnavailableError("retention failed");
      });
  EXPECT_EQ(refused.code(), absl::StatusCode::kUnavailable);
  EXPECT_TRUE(controller->review().has_value());

  ASSERT_OK(controller->AcceptCandidate(
      [](const ImageGenerationReview&, const ImageGenerationCandidate&) {
        return absl::OkStatus();
      }));
  EXPECT_FALSE(controller->review().has_value());

  ASSERT_OK(controller->Submit(SpecFor("discard me")));
  ASSERT_OK(engine->Run().status());
  ASSERT_OK_AND_ASSIGN(const bool discarded_completed, controller->Poll());
  ASSERT_TRUE(discarded_completed);
  controller->DiscardCandidates();
  EXPECT_FALSE(controller->review().has_value());
}

}  // namespace
}  // namespace zebes
