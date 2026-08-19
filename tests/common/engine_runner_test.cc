#include "common/engine_runner.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "common/blocking_callback_thread.h"
#include "common/engine.h"
#include "common/mpsc_queue.h"
#include "common/notification.h"
#include "common/notification_set.h"
#include "common/status_macros.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

// An engine with no wake sources of its own. The runner still seals a set that
// holds its stop source.
class SourcelessEngine : public Engine {
 public:
  NotificationSet& notification_set() override { return *notification_set_; }

 protected:
  explicit SourcelessEngine(std::unique_ptr<NotificationSet> notification_set)
      : notification_set_(std::move(notification_set)) {}

 private:
  std::unique_ptr<NotificationSet> notification_set_;
};

class NotifyQueueEngine final : public Engine {
 public:
  static absl::StatusOr<std::unique_ptr<NotifyQueueEngine>> Create() {
    ASSIGN_OR_RETURN(std::unique_ptr<NotificationSet> notification_set, NotificationSet::Create());
    ASSIGN_OR_RETURN(Notification * notification, notification_set->AddSoftware());
    return std::unique_ptr<NotifyQueueEngine>(
        new NotifyQueueEngine(std::move(notification_set), *notification));
  }

  NotificationSet& notification_set() override { return *notification_set_; }

  absl::StatusOr<RunResult> Run() override {
    std::optional<int> value = queue_.TryPop();
    if (!value.has_value()) {
      return RunResult{.feedback = RunFeedback::kIdle};
    }
    values_.push_back(*value);
    processed_.fetch_add(1, std::memory_order_release);
    processed_.notify_all();
    return RunResult{.feedback = RunFeedback::kDidWork};
  }

  bool Push(int value) { return queue_.TryPush(std::move(value)); }

  void WaitUntilProcessed(size_t count) const {
    size_t processed = processed_.load(std::memory_order_acquire);
    while (processed < count) {
      processed_.wait(processed, std::memory_order_acquire);
      processed = processed_.load(std::memory_order_acquire);
    }
  }

  const std::vector<int>& values() const { return values_; }

 private:
  NotifyQueueEngine(std::unique_ptr<NotificationSet> notification_set, Notification& notification)
      : notification_set_(std::move(notification_set)), queue_(notification) {}

  std::unique_ptr<NotificationSet> notification_set_;
  MpscNotifyQueue<int, 8> queue_;
  std::vector<int> values_;
  mutable std::atomic<size_t> processed_ = 0;
};

class FailingEngine final : public SourcelessEngine {
 public:
  static absl::StatusOr<std::unique_ptr<FailingEngine>> Create() {
    ASSIGN_OR_RETURN(std::unique_ptr<NotificationSet> notification_set, NotificationSet::Create());
    return std::unique_ptr<FailingEngine>(new FailingEngine(std::move(notification_set)));
  }

  absl::StatusOr<RunResult> Run() override { return absl::DataLossError("engine failed"); }

 private:
  using SourcelessEngine::SourcelessEngine;
};

class InvalidFeedbackEngine final : public SourcelessEngine {
 public:
  static absl::StatusOr<std::unique_ptr<InvalidFeedbackEngine>> Create() {
    ASSIGN_OR_RETURN(std::unique_ptr<NotificationSet> notification_set, NotificationSet::Create());
    return std::unique_ptr<InvalidFeedbackEngine>(
        new InvalidFeedbackEngine(std::move(notification_set)));
  }

  absl::StatusOr<RunResult> Run() override {
    return RunResult{.feedback = static_cast<RunFeedback>(-1)};
  }

 private:
  using SourcelessEngine::SourcelessEngine;
};

class IdleEngine final : public SourcelessEngine {
 public:
  static absl::StatusOr<std::unique_ptr<IdleEngine>> Create() {
    ASSIGN_OR_RETURN(std::unique_ptr<NotificationSet> notification_set, NotificationSet::Create());
    return std::unique_ptr<IdleEngine>(new IdleEngine(std::move(notification_set)));
  }

  absl::StatusOr<RunResult> Run() override {
    ran_.store(true, std::memory_order_release);
    ran_.notify_all();
    return RunResult{.feedback = RunFeedback::kIdle};
  }

  void WaitUntilRun() const { ran_.wait(false, std::memory_order_acquire); }

 private:
  using SourcelessEngine::SourcelessEngine;

  mutable std::atomic<bool> ran_ = false;
};

class WakeRaceEngine final : public Engine {
 public:
  static absl::StatusOr<std::unique_ptr<WakeRaceEngine>> Create() {
    ASSIGN_OR_RETURN(std::unique_ptr<NotificationSet> notification_set, NotificationSet::Create());
    ASSIGN_OR_RETURN(Notification * notification, notification_set->AddSoftware());
    return std::unique_ptr<WakeRaceEngine>(
        new WakeRaceEngine(std::move(notification_set), *notification));
  }

  NotificationSet& notification_set() override { return *notification_set_; }

  absl::StatusOr<RunResult> Run() override {
    std::optional<int> value = queue_.TryPop();
    if (value.has_value()) {
      processed_.store(true, std::memory_order_release);
      processed_.notify_all();
      return RunResult{.feedback = RunFeedback::kDidWork};
    }
    if (!checked_empty_.exchange(true, std::memory_order_acq_rel)) {
      checked_empty_.notify_all();
      wait_for_return_.wait(false, std::memory_order_acquire);
    }
    return RunResult{.feedback = RunFeedback::kIdle};
  }

  bool Push(int value) { return queue_.TryPush(std::move(value)); }

  void WaitUntilEmptyWasChecked() const { checked_empty_.wait(false, std::memory_order_acquire); }

  void AllowIdleReturn() {
    wait_for_return_.store(true, std::memory_order_release);
    wait_for_return_.notify_all();
  }

  void WaitUntilProcessed() const { processed_.wait(false, std::memory_order_acquire); }

 private:
  WakeRaceEngine(std::unique_ptr<NotificationSet> notification_set, Notification& notification)
      : notification_set_(std::move(notification_set)), queue_(notification) {}

  std::unique_ptr<NotificationSet> notification_set_;
  MpscNotifyQueue<int, 1> queue_;
  mutable std::atomic<bool> checked_empty_ = false;
  std::atomic<bool> wait_for_return_ = false;
  mutable std::atomic<bool> processed_ = false;
};

class TwoQueueEngine final : public Engine {
 public:
  static absl::StatusOr<std::unique_ptr<TwoQueueEngine>> Create() {
    ASSIGN_OR_RETURN(std::unique_ptr<NotificationSet> notification_set, NotificationSet::Create());
    ASSIGN_OR_RETURN(Notification * first, notification_set->AddSoftware());
    ASSIGN_OR_RETURN(Notification * second, notification_set->AddSoftware());
    return std::unique_ptr<TwoQueueEngine>(
        new TwoQueueEngine(std::move(notification_set), *first, *second));
  }

  NotificationSet& notification_set() override { return *notification_set_; }

  absl::StatusOr<RunResult> Run() override {
    std::optional<int> value = first_.TryPop();
    if (!value.has_value()) {
      value = second_.TryPop();
    }
    if (!value.has_value()) {
      return RunResult{.feedback = RunFeedback::kIdle};
    }
    values_.push_back(*value);
    processed_.fetch_add(1, std::memory_order_release);
    processed_.notify_all();
    return RunResult{.feedback = RunFeedback::kDidWork};
  }

  bool PushFirst(int value) { return first_.TryPush(std::move(value)); }
  bool PushSecond(int value) { return second_.TryPush(std::move(value)); }

  void WaitUntilProcessed(size_t count) const {
    size_t processed = processed_.load(std::memory_order_acquire);
    while (processed < count) {
      processed_.wait(processed, std::memory_order_acquire);
      processed = processed_.load(std::memory_order_acquire);
    }
  }

  const std::vector<int>& values() const { return values_; }

 private:
  TwoQueueEngine(std::unique_ptr<NotificationSet> notification_set,
                 Notification& first_notification, Notification& second_notification)
      : notification_set_(std::move(notification_set)),
        first_(first_notification),
        second_(second_notification) {}

  std::unique_ptr<NotificationSet> notification_set_;
  MpscNotifyQueue<int, 2> first_;
  MpscNotifyQueue<int, 2> second_;
  std::vector<int> values_;
  mutable std::atomic<size_t> processed_ = 0;
};

// Arm/disarm state must outlive the callbacks the set stores, so it is built
// before the set and captured by pointer.
class ArmRaceEngine final : public Engine {
 public:
  static absl::StatusOr<std::unique_ptr<ArmRaceEngine>> Create() {
    auto state = std::make_unique<ArmState>();
    ArmState* const state_pointer = state.get();
    ASSIGN_OR_RETURN(std::unique_ptr<NotificationSet> notification_set, NotificationSet::Create());
    RETURN_IF_ERROR(
        notification_set
            ->AddSoftware(NotificationCallbacks(
                [state_pointer] {
                  if (state_pointer->arm_count.fetch_add(1, std::memory_order_acq_rel) == 0) {
                    state_pointer->work_ready.store(true, std::memory_order_release);
                  }
                  return absl::OkStatus();
                },
                [state_pointer]() noexcept {
                  state_pointer->disarm_count.fetch_add(1, std::memory_order_release);
                }))
            .status());
    return std::unique_ptr<ArmRaceEngine>(
        new ArmRaceEngine(std::move(state), std::move(notification_set)));
  }

  NotificationSet& notification_set() override { return *notification_set_; }

  absl::StatusOr<RunResult> Run() override {
    if (!state_->work_ready.exchange(false, std::memory_order_acq_rel)) {
      return RunResult{.feedback = RunFeedback::kIdle};
    }
    processed_.store(true, std::memory_order_release);
    processed_.notify_all();
    return RunResult{.feedback = RunFeedback::kDidWork};
  }

  void WaitUntilProcessed() const { processed_.wait(false, std::memory_order_acquire); }

  size_t arm_count() const { return state_->arm_count.load(std::memory_order_acquire); }
  size_t disarm_count() const { return state_->disarm_count.load(std::memory_order_acquire); }

 private:
  struct ArmState {
    std::atomic<size_t> arm_count = 0;
    std::atomic<size_t> disarm_count = 0;
    std::atomic<bool> work_ready = false;
  };

  ArmRaceEngine(std::unique_ptr<ArmState> state, std::unique_ptr<NotificationSet> notification_set)
      : state_(std::move(state)), notification_set_(std::move(notification_set)) {}

  // The set is destroyed first so its callbacks cannot outlive the state.
  std::unique_ptr<ArmState> state_;
  std::unique_ptr<NotificationSet> notification_set_;
  mutable std::atomic<bool> processed_ = false;
};

// Never has work and never notifies, so how often it is asked is entirely the
// runner's choice. It stands in for the two sources that cannot be waited on
// natively: a remote transfer with no registered descriptor, and a timestep.
class DeadlineEngine final : public SourcelessEngine {
 public:
  static absl::StatusOr<std::unique_ptr<DeadlineEngine>> Create(
      std::optional<absl::Duration> poll_interval) {
    ASSIGN_OR_RETURN(std::unique_ptr<NotificationSet> notification_set, NotificationSet::Create());
    return std::unique_ptr<DeadlineEngine>(
        new DeadlineEngine(std::move(notification_set), poll_interval));
  }

  absl::StatusOr<RunResult> Run() override {
    passes_.fetch_add(1, std::memory_order_acq_rel);
    passes_.notify_all();
    if (!poll_interval_.has_value()) {
      return RunResult{.feedback = RunFeedback::kIdle};
    }
    return RunResult{
        .feedback = RunFeedback::kIdle,
        .wake_deadline = absl::Now() + *poll_interval_,
    };
  }

  size_t passes() const { return passes_.load(std::memory_order_acquire); }

  void WaitUntilPasses(size_t count) const {
    size_t passes = passes_.load(std::memory_order_acquire);
    while (passes < count) {
      passes_.wait(passes, std::memory_order_acquire);
      passes = passes_.load(std::memory_order_acquire);
    }
  }

 private:
  DeadlineEngine(std::unique_ptr<NotificationSet> notification_set,
                 std::optional<absl::Duration> poll_interval)
      : SourcelessEngine(std::move(notification_set)), poll_interval_(poll_interval) {}

  std::optional<absl::Duration> poll_interval_;
  mutable std::atomic<size_t> passes_ = 0;
};

TEST(BlockingCallbackThreadTest, RunsTheCallbackAndSupportsRepeatedWaits) {
  std::atomic<bool> ran = false;
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread, BlockingCallbackThread::Start([&ran] {
                         ran.store(true, std::memory_order_release);
                         return absl::OkStatus();
                       }));

  EXPECT_TRUE(thread.Wait().ok());
  EXPECT_TRUE(ran.load(std::memory_order_acquire));
  EXPECT_TRUE(thread.Wait().ok());
}

TEST(BlockingCallbackThreadTest, TranslatesAnEscapedException) {
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start(
                           []() -> absl::Status { throw std::runtime_error("callback broke"); }));

  const absl::Status status = thread.Wait();
  EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
  EXPECT_EQ(status.message(), "Blocking thread callback threw an exception: callback broke");
}

TEST(BlockingCallbackThreadTest, ReturnsTheCallbackFailure) {
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread, BlockingCallbackThread::Start([] {
                         return absl::DataLossError("callback failed");
                       }));

  const absl::Status status = thread.Wait();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(status.message(), "callback failed");
}

TEST(BlockingCallbackThreadTest, RejectsAnEmptyCallback) {
  const absl::Status status = BlockingCallbackThread::Start({}).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "Blocking thread callback is empty");
}

TEST(BlockingCallbackThreadTest, RejectsWaitingOnAMovedFromThread) {
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([] { return absl::OkStatus(); }));
  BlockingCallbackThread moved_thread(std::move(thread));

  const absl::Status status = thread.Wait();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message(), "Blocking thread has no callback");
  ASSERT_OK(moved_thread.Wait());
}

TEST(EngineRunnerTest, SleepsUntilNotifyQueueWorkArrivesAndStopsPromptly) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotifyQueueEngine> engine, NotifyQueueEngine::Create());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));

  for (int next : {3, 5, 8}) {
    ASSERT_TRUE(engine->Push(next));
  }
  engine->WaitUntilProcessed(3);

  runner->Stop();
  ASSERT_OK(thread.Wait());
  EXPECT_EQ(engine->values(), (std::vector<int>{3, 5, 8}));
}

// Nothing is armed before the runner starts, so every one of these pushes
// skips its wake syscall. The runner's first poll is what delivers them.
TEST(EngineRunnerTest, ProcessesWorkPublishedBeforeTheRunnerStarts) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotifyQueueEngine> engine, NotifyQueueEngine::Create());
  for (int next : {2, 4, 6}) {
    ASSERT_TRUE(engine->Push(next));
  }

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));
  engine->WaitUntilProcessed(3);

  runner->Stop();
  ASSERT_OK(thread.Wait());
  EXPECT_EQ(engine->values(), (std::vector<int>{2, 4, 6}));
}

TEST(EngineRunnerTest, AcceptsMultipleNotificationSources) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TwoQueueEngine> engine, TwoQueueEngine::Create());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));

  ASSERT_TRUE(engine->PushFirst(21));
  engine->WaitUntilProcessed(1);
  ASSERT_TRUE(engine->PushSecond(34));
  engine->WaitUntilProcessed(2);

  runner->Stop();
  ASSERT_OK(thread.Wait());
  EXPECT_EQ(engine->values(), (std::vector<int>{21, 34}));
}

TEST(EngineRunnerTest, ReturnsAnEngineFailure) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<FailingEngine> engine, FailingEngine::Create());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));

  const absl::Status status = thread.Wait();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(status.message(), "engine failed");

  const absl::Status second_run = runner->Run();
  EXPECT_EQ(second_run.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(second_run.message(), "Engine runner has already started or stopped");
}

TEST(EngineRunnerTest, RejectsInvalidRunFeedback) {
  EXPECT_DEATH(
      {
        absl::StatusOr<std::unique_ptr<InvalidFeedbackEngine>> engine =
            InvalidFeedbackEngine::Create();
        absl::StatusOr<std::unique_ptr<EngineRunner>> runner = EngineRunner::Create(**engine);
        (void)(*runner)->Run();
      },
      "Engine returned invalid run feedback");
}

TEST(EngineRunnerTest, RejectsASecondRunnerForTheSameEngine) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<IdleEngine> engine, IdleEngine::Create());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));

  const absl::Status status = EngineRunner::Create(*engine).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message(), "NotificationSet is sealed");
}

TEST(EngineRunnerTest, RejectsAConcurrentRun) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<IdleEngine> engine, IdleEngine::Create());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));
  engine->WaitUntilRun();

  const absl::Status status = runner->Run();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message(), "Engine runner has already started or stopped");
  runner->Stop();
  ASSERT_OK(thread.Wait());
}

TEST(EngineRunnerTest, StopBeforeRunPreventsStartup) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<IdleEngine> engine, IdleEngine::Create());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));

  runner->Stop();
  runner->Stop();
  const absl::Status status = runner->Run();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message(), "Engine runner has already started or stopped");
}

// The push lands while the engine is inside its idle check, before the runner
// arms, so the producer reads the set unarmed and sends no wake at all. Only
// the recheck pass between Arm and Wait can deliver it.
TEST(EngineRunnerTest, DoesNotLoseAWakeBetweenIdleCheckAndWait) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<WakeRaceEngine> engine, WakeRaceEngine::Create());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));

  engine->WaitUntilEmptyWasChecked();
  ASSERT_TRUE(engine->Push(13));
  engine->AllowIdleReturn();
  engine->WaitUntilProcessed();

  runner->Stop();
  ASSERT_OK(thread.Wait());
}

TEST(EngineRunnerTest, RechecksForWorkAfterArmingExternalNotifications) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ArmRaceEngine> engine, ArmRaceEngine::Create());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));

  engine->WaitUntilProcessed();
  runner->Stop();
  ASSERT_OK(thread.Wait());

  EXPECT_GE(engine->arm_count(), 1);
  EXPECT_GE(engine->disarm_count(), 1);
}

// An idle engine with a deadline must be re-asked at roughly its own cadence:
// often enough to make progress on a source it can only poll, and nowhere near
// often enough to be a spin. Before deadlines existed the only way to be
// re-asked was to claim kDidWork, which returned here without sleeping at all
// and would drive the pass count into the millions.
TEST(EngineRunnerTest, PollsAtTheEngineDeadlineWithoutSpinning) {
  constexpr absl::Duration kPollInterval = absl::Milliseconds(20);
  constexpr absl::Duration kObserved = absl::Milliseconds(400);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<DeadlineEngine> engine,
                       DeadlineEngine::Create(kPollInterval));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));

  // Four intervals' worth of passes proves the deadline actually fires and is
  // not one lucky wake. Each cycle costs two passes: the idle check and the
  // post-arm recheck.
  engine->WaitUntilPasses(8);
  absl::SleepFor(kObserved);
  runner->Stop();
  ASSERT_OK(thread.Wait());

  // A spin executes passes as fast as the CPU allows. The generous ceiling here
  // is deliberate: it separates polling from spinning by orders of magnitude
  // without asserting a scheduling accuracy no timer guarantees.
  const size_t ceiling = 20 * static_cast<size_t>(kObserved / kPollInterval);
  EXPECT_LE(engine->passes(), ceiling);
}

// The guarantee a process-lifetime engine rests on: with every source
// represented by a notification, an idle engine is asked once, sleeps, and
// costs nothing until something notifies it.
TEST(EngineRunnerTest, SleepsIndefinitelyWithoutADeadline) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<DeadlineEngine> engine,
                       DeadlineEngine::Create(std::nullopt));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));

  engine->WaitUntilPasses(2);
  absl::SleepFor(absl::Milliseconds(200));
  // Sampled before Stop, because stopping wakes the runner into a final pass.
  const size_t passes_while_sleeping = engine->passes();
  runner->Stop();
  ASSERT_OK(thread.Wait());

  // Exactly the idle check and its post-arm recheck. Nothing notified, so a
  // third pass would mean the runner woke itself.
  EXPECT_EQ(passes_while_sleeping, 2);
}

}  // namespace
}  // namespace zebes
