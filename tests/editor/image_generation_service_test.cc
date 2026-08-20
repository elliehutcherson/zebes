#include "editor/image_generation/image_generation_service.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "editor/image_generation/codex_image_client.h"
#include "editor/image_generation/image_generation.h"
#include "editor/image_generation/image_generation_engine.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

// Long enough that a loaded machine cannot fail a passing test, short enough
// that a genuine hang still ends the run.
constexpr absl::Duration kEventTimeout = absl::Seconds(10);

RgbaImage OnePixelImage() { return RgbaImage{.width = 1, .height = 1, .pixels = {7, 8, 9, 255}}; }

ImageGenerationSpec SpecFor(absl::string_view prompt) {
  return ImageGenerationSpec{
      .prompt = std::string(prompt),
      .requested_candidates = 1,
      .target_aspect = {.width = 1, .height = 1},
  };
}

// Outlives the service, so a test can still read what an abandoned request did
// after the service that owned it is gone.
struct OperationState {
  std::atomic<int> polls = 0;
  std::atomic<int> cancellations = 0;
};

class FakeOperation final : public ImageGenerationOperation {
 public:
  FakeOperation(bool completes, std::string prompt, std::shared_ptr<OperationState> state)
      : completes_(completes), prompt_(std::move(prompt)), state_(std::move(state)) {}

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll() override {
    state_->polls.fetch_add(1, std::memory_order_acq_rel);
    if (!completes_) return std::nullopt;
    return std::optional<ImageGenerationResult>(ImageGenerationResult{
        .provider = "fake",
        .model = "fake-model",
        .submitted_prompt = prompt_,
        .provider_request_id = "request-1",
        .candidates = {{.image = OnePixelImage(), .revised_prompt = std::nullopt}},
    });
  }

  void Cancel() noexcept override { state_->cancellations.fetch_add(1, std::memory_order_acq_rel); }

  // Short enough that a never-completing request keeps the engine's deadline
  // path live without making the shutdown test wait on a sleeping runner.
  absl::Duration SuggestedPollDelay() const override { return absl::Milliseconds(1); }

 private:
  bool completes_;
  std::string prompt_;
  std::shared_ptr<OperationState> state_;
};

class FakeClient final : public ImageGenerationClient {
 public:
  FakeClient(bool completes, std::shared_ptr<OperationState> state)
      : completes_(completes), state_(std::move(state)) {}

  ImageGenerationCapabilities Capabilities() const override {
    return ImageGenerationCapabilities{.maximum_candidates = 4};
  }

 protected:
  absl::StatusOr<ImageGenerationRequest> StartValidated(ImageGenerationSpec spec) override {
    return ImageGenerationRequest::Create(
        std::make_unique<FakeOperation>(completes_, std::move(spec.prompt), state_));
  }

 private:
  bool completes_;
  std::shared_ptr<OperationState> state_;
};

// Drains on the owning thread the way an editor frame loop does, rather than
// sleeping on the engine, which NextEvent deliberately does not support.
std::optional<GenerationEvent> WaitForEvent(ImageGenerationEngine& engine) {
  const absl::Time deadline = absl::Now() + kEventTimeout;
  while (absl::Now() < deadline) {
    std::optional<GenerationEvent> event = engine.NextEvent();
    if (event.has_value()) return event;
  }
  return std::nullopt;
}

TEST(ImageGenerationServiceTest, RejectsAMissingClient) {
  const absl::Status status = ImageGenerationService::Create(nullptr).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "Image generation service requires a client");
}

TEST(ImageGenerationServiceTest, RejectsAnInvalidCodexConfigurationBeforeStartingARunner) {
  CodexImageConfig config;
  config.process.executable.clear();

  const absl::Status status = ImageGenerationService::CreateCodex(config).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

// The wiring this class exists for: a caller that only ever touches engine()
// gets its result without starting or stopping anything itself.
TEST(ImageGenerationServiceTest, RunsASubmittedRequestOnItsOwnThread) {
  auto state = std::make_shared<OperationState>();
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ImageGenerationService> service,
      ImageGenerationService::Create(std::make_unique<FakeClient>(/*completes=*/true, state)));

  ASSERT_OK_AND_ASSIGN(const uint64_t id, service->engine().Submit(SpecFor("a mossy boulder")));
  const std::optional<GenerationEvent> event = WaitForEvent(service->engine());

  ASSERT_TRUE(event.has_value());
  EXPECT_EQ(event->id, id);
  ASSERT_TRUE(event->result.ok());
  EXPECT_EQ(event->result->submitted_prompt, "a mossy boulder");
}

// Destruction must stop the runner before joining its thread; without that
// stop the join would never return and this test would hang rather than fail.
TEST(ImageGenerationServiceTest, ShutdownCancelsAnUnfinishedRequest) {
  auto state = std::make_shared<OperationState>();
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ImageGenerationService> service,
      ImageGenerationService::Create(std::make_unique<FakeClient>(/*completes=*/false, state)));
  ASSERT_OK(service->engine().Submit(SpecFor("a mossy boulder")).status());

  const absl::Time deadline = absl::Now() + kEventTimeout;
  while (state->polls.load(std::memory_order_acquire) == 0 && absl::Now() < deadline) {
  }
  ASSERT_GT(state->polls.load(std::memory_order_acquire), 0);
  service.reset();

  EXPECT_EQ(state->cancellations.load(std::memory_order_acquire), 1);
}

}  // namespace
}  // namespace zebes
