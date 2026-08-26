#include "generation/image_generation_engine.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "common/blocking_callback_thread.h"
#include "common/engine_runner.h"
#include "common/status_macros.h"
#include "generation/image_generation.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

RgbaImage OnePixelImage() { return RgbaImage{.width = 1, .height = 1, .pixels = {1, 2, 3, 255}}; }

ImageGenerationResult ResultFor(absl::string_view prompt) {
  return ImageGenerationResult{
      .provider = "fake",
      .model = "fake-model",
      .submitted_prompt = std::string(prompt),
      .provider_request_id = "request-1",
      .candidates = {{.image = OnePixelImage(), .revised_prompt = std::nullopt}},
  };
}

ImageGenerationSpec SpecFor(absl::string_view prompt) {
  return ImageGenerationSpec{
      .prompt = std::string(prompt),
      .requested_candidates = 1,
      .target_aspect = {.width = 1, .height = 1},
  };
}

// Shared with the test thread, so a test can tell when a request has actually
// reached the engine rather than guessing at timing.
struct OperationState {
  std::atomic<int> polls = 0;
  std::atomic<int> cancellations = 0;
};

// Completes after a fixed number of polls, so a test can hold a request in
// flight without any network or timing dependency.
class FakeOperation final : public ImageGenerationOperation {
 public:
  FakeOperation(int polls_until_done, ImageGenerationResult result,
                std::shared_ptr<OperationState> state, absl::Duration poll_delay)
      : polls_until_done_(polls_until_done),
        result_(std::move(result)),
        state_(std::move(state)),
        poll_delay_(poll_delay) {}

  ~FakeOperation() override = default;

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll() override {
    state_->polls.fetch_add(1, std::memory_order_acq_rel);
    if (++polls_ < polls_until_done_) return std::nullopt;
    return std::optional<ImageGenerationResult>(std::move(result_));
  }

  void Cancel() noexcept override { state_->cancellations.fetch_add(1, std::memory_order_acq_rel); }

  absl::Duration SuggestedPollDelay() const override { return poll_delay_; }

 private:
  int polls_ = 0;
  int polls_until_done_;
  ImageGenerationResult result_;
  std::shared_ptr<OperationState> state_;
  absl::Duration poll_delay_;
};

class FakeClient final : public ImageGenerationClient {
 public:
  explicit FakeClient(std::shared_ptr<OperationState> state) : state_(std::move(state)) {}

  ImageGenerationCapabilities Capabilities() const override {
    return ImageGenerationCapabilities{.maximum_candidates = 4};
  }

  void set_polls_until_done(int polls) { polls_until_done_ = polls; }
  void set_poll_delay(absl::Duration delay) { poll_delay_ = delay; }
  void set_start_failure(absl::Status status) { start_failure_ = std::move(status); }

 protected:
  absl::StatusOr<ImageGenerationRequest> StartValidated(ImageGenerationSpec spec) override {
    if (!start_failure_.ok()) return start_failure_;
    return ImageGenerationRequest::Create(std::make_unique<FakeOperation>(
        polls_until_done_, ResultFor(spec.prompt), state_, poll_delay_));
  }

 private:
  std::shared_ptr<OperationState> state_;
  int polls_until_done_ = 1;
  absl::Duration poll_delay_ = absl::Milliseconds(1);
  absl::Status start_failure_;
};

struct EngineFixture {
  std::shared_ptr<OperationState> state = std::make_shared<OperationState>();
  FakeClient* client = nullptr;
  std::unique_ptr<ImageGenerationEngine> engine;

  int cancellations() const { return state->cancellations.load(std::memory_order_acquire); }

  // Fails rather than proceeding on a guess, so a shutdown test never races the
  // worker's own startup.
  bool WaitUntilPolled() const {
    const absl::Time deadline = absl::Now() + absl::Seconds(10);
    while (absl::Now() < deadline) {
      if (state->polls.load(std::memory_order_acquire) > 0) return true;
    }
    return false;
  }
};

absl::StatusOr<EngineFixture> MakeEngine() {
  EngineFixture fixture;
  auto client = std::make_unique<FakeClient>(fixture.state);
  fixture.client = client.get();
  ASSIGN_OR_RETURN(fixture.engine, ImageGenerationEngine::Create(std::move(client)));
  return fixture;
}

// Drives Run directly, without a runner or a thread, so a test can assert what
// one pass did and what it asked the runner to do next.
absl::StatusOr<GenerationEvent> RunUntilEvent(ImageGenerationEngine& engine, int maximum_passes) {
  for (int pass = 0; pass < maximum_passes; ++pass) {
    RETURN_IF_ERROR(engine.Run().status());
    std::optional<GenerationEvent> event = engine.NextEvent();
    if (event.has_value()) return *std::move(event);
  }
  return absl::DeadlineExceededError("no generation event arrived");
}

TEST(ImageGenerationEngineTest, RejectsAMissingClient) {
  const absl::Status status = ImageGenerationEngine::Create(nullptr).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "image generation engine needs a client");
}

TEST(ImageGenerationEngineTest, DeliversACompletedRequestUnderItsSubmittedId) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());
  ASSERT_OK_AND_ASSIGN(const uint64_t id, fixture.engine->Submit(SpecFor("a mossy boulder")));

  ASSERT_OK_AND_ASSIGN(const GenerationEvent event, RunUntilEvent(*fixture.engine, 4));

  EXPECT_EQ(event.id, id);
  ASSERT_TRUE(event.result.ok());
  EXPECT_EQ(event.result->submitted_prompt, "a mossy boulder");
  ASSERT_EQ(event.result->candidates.size(), 1);
}

TEST(ImageGenerationEngineTest, TargetedCollectionPreservesAnotherSurfacesEvent) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());
  ASSERT_OK_AND_ASSIGN(const uint64_t first, fixture.engine->Submit(SpecFor("prop")));
  ASSERT_OK_AND_ASSIGN(const uint64_t second, fixture.engine->Submit(SpecFor("parallax")));
  ASSERT_OK(fixture.engine->Run().status());

  std::optional<GenerationEvent> second_event = fixture.engine->NextEvent(second);
  ASSERT_TRUE(second_event.has_value());
  ASSERT_TRUE(second_event->result.ok());
  EXPECT_EQ(second_event->result->submitted_prompt, "parallax");

  std::optional<GenerationEvent> first_event = fixture.engine->NextEvent(first);
  ASSERT_TRUE(first_event.has_value());
  ASSERT_TRUE(first_event->result.ok());
  EXPECT_EQ(first_event->result->submitted_prompt, "prop");
}

// The guarantee a session-lifetime engine rests on: with nothing in flight
// there is no deadline, so the runner sleeps until Submit notifies it.
TEST(ImageGenerationEngineTest, ReportsNoDeadlineWhileNothingIsInFlight) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());

  ASSERT_OK_AND_ASSIGN(const RunResult result, fixture.engine->Run());

  EXPECT_EQ(result.feedback, RunFeedback::kIdle);
  EXPECT_FALSE(result.wake_deadline.has_value());
}

// An in-flight request has no descriptor to wait on, so the pass that leaves it
// pending must hand the runner a deadline instead of letting it sleep.
TEST(ImageGenerationEngineTest, ReportsTheSoonestRequestDeadlineWhileWorkIsInFlight) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());
  fixture.client->set_polls_until_done(1000);
  fixture.client->set_poll_delay(absl::Milliseconds(40));
  ASSERT_OK(fixture.engine->Submit(SpecFor("slow")).status());

  // The pass that starts and polls the request reports progress.
  ASSERT_OK_AND_ASSIGN(const RunResult started, fixture.engine->Run());
  EXPECT_EQ(started.feedback, RunFeedback::kDidWork);

  const absl::Time before = absl::Now();
  ASSERT_OK_AND_ASSIGN(const RunResult pending, fixture.engine->Run());

  EXPECT_EQ(pending.feedback, RunFeedback::kIdle);
  ASSERT_TRUE(pending.wake_deadline.has_value());
  EXPECT_GT(*pending.wake_deadline, before);
  EXPECT_LE(*pending.wake_deadline, before + absl::Milliseconds(40) + absl::Seconds(1));
}

TEST(ImageGenerationEngineTest, TakesTheSoonestDeadlineAcrossConcurrentRequests) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());
  fixture.client->set_polls_until_done(1000);
  fixture.client->set_poll_delay(absl::Seconds(30));
  ASSERT_OK(fixture.engine->Submit(SpecFor("patient")).status());
  ASSERT_OK(fixture.engine->Run().status());

  fixture.client->set_poll_delay(absl::Milliseconds(10));
  ASSERT_OK(fixture.engine->Submit(SpecFor("urgent")).status());
  ASSERT_OK(fixture.engine->Run().status());

  const absl::Time before = absl::Now();
  ASSERT_OK_AND_ASSIGN(const RunResult pending, fixture.engine->Run());

  // The patient request must not hold the urgent one's attention hostage.
  ASSERT_TRUE(pending.wake_deadline.has_value());
  EXPECT_LT(*pending.wake_deadline, before + absl::Seconds(30));
}

TEST(ImageGenerationEngineTest, ReportsARejectedSpecAsThatRequestsOutcome) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());
  // Five candidates against a client that caps at four.
  ImageGenerationSpec spec = SpecFor("too many");
  spec.requested_candidates = 5;

  const absl::Status status = fixture.engine->Submit(std::move(spec)).status();

  // Validation happens in Start, so the id is issued and the failure comes back
  // as this request's event rather than as an engine failure.
  ASSERT_TRUE(status.ok()) << status;
  ASSERT_OK_AND_ASSIGN(const GenerationEvent event, RunUntilEvent(*fixture.engine, 4));
  EXPECT_EQ(event.result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ImageGenerationEngineTest, ReportsAStartFailureWithoutEndingTheEngine) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());
  fixture.client->set_start_failure(absl::UnauthenticatedError("no credential"));
  ASSERT_OK_AND_ASSIGN(const uint64_t failed, fixture.engine->Submit(SpecFor("first")));

  ASSERT_OK_AND_ASSIGN(const GenerationEvent event, RunUntilEvent(*fixture.engine, 4));
  EXPECT_EQ(event.id, failed);
  EXPECT_EQ(event.result.status().code(), absl::StatusCode::kUnauthenticated);

  // The engine is still usable, which is the point of not returning the failure
  // from Run: one bad request must not take the session's other requests down.
  fixture.client->set_start_failure(absl::OkStatus());
  ASSERT_OK(fixture.engine->Submit(SpecFor("second")).status());
  ASSERT_OK_AND_ASSIGN(const GenerationEvent second, RunUntilEvent(*fixture.engine, 4));
  EXPECT_TRUE(second.result.ok());
}

TEST(ImageGenerationEngineTest, CancelReportsCancelledAndReleasesTheRequest) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());
  fixture.client->set_polls_until_done(1000);
  ASSERT_OK_AND_ASSIGN(const uint64_t id, fixture.engine->Submit(SpecFor("abandoned")));
  ASSERT_OK(fixture.engine->Run().status());
  ASSERT_EQ(fixture.cancellations(), 0);

  ASSERT_OK(fixture.engine->Cancel(id));
  ASSERT_OK_AND_ASSIGN(const GenerationEvent event, RunUntilEvent(*fixture.engine, 4));

  EXPECT_EQ(event.id, id);
  EXPECT_EQ(event.result.status().code(), absl::StatusCode::kCancelled);
  // Destroying the request is what cancels it, without joining remote work.
  EXPECT_EQ(fixture.cancellations(), 1);
  ASSERT_OK_AND_ASSIGN(const RunResult idle, fixture.engine->Run());
  EXPECT_FALSE(idle.wake_deadline.has_value());
}

// A result already queued is a result the caller asked for. Cancelling after it
// finished must not produce a second event for the same id.
TEST(ImageGenerationEngineTest, CancelIsIgnoredForARequestThatAlreadyFinished) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());
  ASSERT_OK_AND_ASSIGN(const uint64_t id, fixture.engine->Submit(SpecFor("done")));
  ASSERT_OK_AND_ASSIGN(const GenerationEvent event, RunUntilEvent(*fixture.engine, 4));
  ASSERT_TRUE(event.result.ok());

  ASSERT_OK(fixture.engine->Cancel(id));
  ASSERT_OK(fixture.engine->Run().status());

  EXPECT_FALSE(fixture.engine->NextEvent().has_value());
}

TEST(ImageGenerationEngineTest, RefusesSubmissionsBeyondTheOutstandingBound) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());
  fixture.client->set_polls_until_done(1000);
  for (size_t submitted = 0; submitted < ImageGenerationEngine::kMaxOutstandingRequests;
       ++submitted) {
    ASSERT_OK(fixture.engine->Submit(SpecFor("held")).status());
  }

  const absl::Status status = fixture.engine->Submit(SpecFor("one too many")).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
  EXPECT_EQ(status.message(), "too many image generation requests are outstanding");
}

// Collecting an event returns its reserved slot, so a caller that keeps up is
// never refused.
TEST(ImageGenerationEngineTest, CollectingAnEventFreesItsOutstandingSlot) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());
  for (size_t submitted = 0; submitted < ImageGenerationEngine::kMaxOutstandingRequests;
       ++submitted) {
    ASSERT_OK(fixture.engine->Submit(SpecFor("held")).status());
  }
  ASSERT_EQ(fixture.engine->Submit(SpecFor("refused")).status().code(),
            absl::StatusCode::kResourceExhausted);

  ASSERT_OK_AND_ASSIGN(const GenerationEvent event, RunUntilEvent(*fixture.engine, 4));
  ASSERT_TRUE(event.result.ok());

  EXPECT_TRUE(fixture.engine->Submit(SpecFor("accepted")).ok());
}

// The whole point of the engine: submitting from another thread wakes a sleeping
// runner, and the result comes back without the submitter ever blocking.
TEST(ImageGenerationEngineTest, RunsUnderAnEngineRunnerAndWakesOnSubmit) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*fixture.engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));

  ASSERT_OK_AND_ASSIGN(const uint64_t id, fixture.engine->Submit(SpecFor("across threads")));

  std::optional<GenerationEvent> event;
  const absl::Time deadline = absl::Now() + absl::Seconds(10);
  while (!event.has_value() && absl::Now() < deadline) {
    event = fixture.engine->NextEvent();
  }
  runner->Stop();
  ASSERT_OK(thread.Wait());

  ASSERT_TRUE(event.has_value()) << "the runner never delivered the generation event";
  EXPECT_EQ(event->id, id);
  EXPECT_TRUE(event->result.ok());
}

// Stopping does not drain: an unfinished request is abandoned rather than
// awaited, and destroying the engine cancels it without joining remote work.
TEST(ImageGenerationEngineTest, ShutdownAbandonsAnUnfinishedRequest) {
  ASSERT_OK_AND_ASSIGN(EngineFixture fixture, MakeEngine());
  fixture.client->set_polls_until_done(1000);
  fixture.client->set_poll_delay(absl::Milliseconds(5));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*fixture.engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));
  ASSERT_OK(fixture.engine->Submit(SpecFor("interrupted")).status());

  // The runner must be inside Run before Stop, or Stop lands on a runner that
  // has not started and the worker never enters the loop at all.
  ASSERT_TRUE(fixture.WaitUntilPolled()) << "the request never reached the engine";
  runner->Stop();
  ASSERT_OK(thread.Wait());
  EXPECT_FALSE(fixture.engine->NextEvent().has_value());

  fixture.engine.reset();
  EXPECT_EQ(fixture.cancellations(), 1);
}

}  // namespace
}  // namespace zebes
