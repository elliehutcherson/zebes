#include "common/engine_runner.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/blocking_callback_thread.h"
#include "common/engine.h"
#include "common/mpsc_queue.h"
#include "common/notification.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

class NotifyQueueEngine final : public Engine {
 public:
  NotifyQueueEngine(MpscNotifyQueue<int, 8>& queue, Notification& notification)
      : queue_(queue), notifications_{&notification} {}

  absl::Span<Notification* const> Notifications() const override { return notifications_; }

  absl::StatusOr<RunFeedback> Run() override {
    std::optional<int> value = queue_.TryPop();
    if (!value.has_value()) {
      return RunFeedback::kIdle;
    }
    values_.push_back(*value);
    processed_.fetch_add(1, std::memory_order_release);
    processed_.notify_all();
    return RunFeedback::kDidWork;
  }

  void WaitUntilProcessed(size_t count) const {
    size_t processed = processed_.load(std::memory_order_acquire);
    while (processed < count) {
      processed_.wait(processed, std::memory_order_acquire);
      processed = processed_.load(std::memory_order_acquire);
    }
  }

  const std::vector<int>& values() const { return values_; }

 private:
  MpscNotifyQueue<int, 8>& queue_;
  std::array<Notification*, 1> notifications_;
  std::vector<int> values_;
  mutable std::atomic<size_t> processed_ = 0;
};

class FailingEngine final : public Engine {
 public:
  absl::Span<Notification* const> Notifications() const override { return {}; }

  absl::StatusOr<RunFeedback> Run() override { return absl::DataLossError("engine failed"); }
};

class InvalidFeedbackEngine final : public Engine {
 public:
  absl::Span<Notification* const> Notifications() const override { return {}; }

  absl::StatusOr<RunFeedback> Run() override { return static_cast<RunFeedback>(-1); }
};

class NullNotificationEngine final : public Engine {
 public:
  absl::Span<Notification* const> Notifications() const override { return notifications_; }

  absl::StatusOr<RunFeedback> Run() override { return RunFeedback::kIdle; }

 private:
  std::array<Notification*, 1> notifications_ = {nullptr};
};

class IdleEngine final : public Engine {
 public:
  absl::Span<Notification* const> Notifications() const override { return {}; }

  absl::StatusOr<RunFeedback> Run() override {
    ran_.store(true, std::memory_order_release);
    ran_.notify_all();
    return RunFeedback::kIdle;
  }

  void WaitUntilRun() const { ran_.wait(false, std::memory_order_acquire); }

 private:
  mutable std::atomic<bool> ran_ = false;
};

class WakeRaceEngine final : public Engine {
 public:
  WakeRaceEngine(MpscNotifyQueue<int, 1>& queue, Notification& notification)
      : queue_(queue), notifications_{&notification} {}

  absl::Span<Notification* const> Notifications() const override { return notifications_; }

  absl::StatusOr<RunFeedback> Run() override {
    std::optional<int> value = queue_.TryPop();
    if (value.has_value()) {
      processed_.store(true, std::memory_order_release);
      processed_.notify_all();
      return RunFeedback::kDidWork;
    }
    if (!checked_empty_.exchange(true, std::memory_order_acq_rel)) {
      checked_empty_.notify_all();
      wait_for_return_.wait(false, std::memory_order_acquire);
    }
    return RunFeedback::kIdle;
  }

  void WaitUntilEmptyWasChecked() const { checked_empty_.wait(false, std::memory_order_acquire); }

  void AllowIdleReturn() {
    wait_for_return_.store(true, std::memory_order_release);
    wait_for_return_.notify_all();
  }

  void WaitUntilProcessed() const { processed_.wait(false, std::memory_order_acquire); }

 private:
  MpscNotifyQueue<int, 1>& queue_;
  std::array<Notification*, 1> notifications_;
  mutable std::atomic<bool> checked_empty_ = false;
  std::atomic<bool> wait_for_return_ = false;
  mutable std::atomic<bool> processed_ = false;
};

class TwoQueueEngine final : public Engine {
 public:
  TwoQueueEngine(MpscNotifyQueue<int, 2>& first, MpscNotifyQueue<int, 2>& second,
                 Notification& first_notification, Notification& second_notification)
      : first_(first), second_(second), notifications_{&first_notification, &second_notification} {}

  absl::Span<Notification* const> Notifications() const override { return notifications_; }

  absl::StatusOr<RunFeedback> Run() override {
    std::optional<int> value = first_.TryPop();
    if (!value.has_value()) {
      value = second_.TryPop();
    }
    if (!value.has_value()) {
      return RunFeedback::kIdle;
    }
    values_.push_back(*value);
    processed_.fetch_add(1, std::memory_order_release);
    processed_.notify_all();
    return RunFeedback::kDidWork;
  }

  void WaitUntilProcessed(size_t count) const {
    size_t processed = processed_.load(std::memory_order_acquire);
    while (processed < count) {
      processed_.wait(processed, std::memory_order_acquire);
      processed = processed_.load(std::memory_order_acquire);
    }
  }

  const std::vector<int>& values() const { return values_; }

 private:
  MpscNotifyQueue<int, 2>& first_;
  MpscNotifyQueue<int, 2>& second_;
  std::array<Notification*, 2> notifications_;
  std::vector<int> values_;
  mutable std::atomic<size_t> processed_ = 0;
};

class ArmRaceEngine final : public Engine {
 public:
  ArmRaceEngine()
      : notification_(NotificationCallbacks(
            [this] {
              if (arm_count_.fetch_add(1, std::memory_order_acq_rel) == 0) {
                work_ready_.store(true, std::memory_order_release);
              }
              return absl::OkStatus();
            },
            [this]() noexcept { disarm_count_.fetch_add(1, std::memory_order_release); })),
        notifications_{&notification_} {}

  absl::Span<Notification* const> Notifications() const override { return notifications_; }

  absl::StatusOr<RunFeedback> Run() override {
    if (!work_ready_.exchange(false, std::memory_order_acq_rel)) {
      return RunFeedback::kIdle;
    }
    processed_.store(true, std::memory_order_release);
    processed_.notify_all();
    return RunFeedback::kDidWork;
  }

  void WaitUntilProcessed() const { processed_.wait(false, std::memory_order_acquire); }

  size_t arm_count() const { return arm_count_.load(std::memory_order_acquire); }
  size_t disarm_count() const { return disarm_count_.load(std::memory_order_acquire); }

 private:
  Notification notification_;
  std::array<Notification*, 1> notifications_;
  std::atomic<size_t> arm_count_ = 0;
  std::atomic<size_t> disarm_count_ = 0;
  std::atomic<bool> work_ready_ = false;
  mutable std::atomic<bool> processed_ = false;
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
  Notification notification;
  MpscNotifyQueue<int, 8> queue(notification);
  NotifyQueueEngine engine(queue, notification);
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));

  for (int next : {3, 5, 8}) {
    int value = next;
    ASSERT_TRUE(queue.TryPush(std::move(value)));
  }
  engine.WaitUntilProcessed(3);

  runner->Stop();
  ASSERT_OK(thread.Wait());
  EXPECT_EQ(engine.values(), (std::vector<int>{3, 5, 8}));
}

TEST(EngineRunnerTest, AcceptsMultipleNotificationSources) {
  Notification first_notification;
  Notification second_notification;
  MpscNotifyQueue<int, 2> first(first_notification);
  MpscNotifyQueue<int, 2> second(second_notification);
  TwoQueueEngine engine(first, second, first_notification, second_notification);
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));

  int first_value = 21;
  ASSERT_TRUE(first.TryPush(std::move(first_value)));
  engine.WaitUntilProcessed(1);
  int second_value = 34;
  ASSERT_TRUE(second.TryPush(std::move(second_value)));
  engine.WaitUntilProcessed(2);

  runner->Stop();
  ASSERT_OK(thread.Wait());
  EXPECT_EQ(engine.values(), (std::vector<int>{21, 34}));
}

TEST(EngineRunnerTest, ReturnsAnEngineFailure) {
  FailingEngine engine;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(engine));
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
        InvalidFeedbackEngine engine;
        absl::StatusOr<std::unique_ptr<EngineRunner>> runner = EngineRunner::Create(engine);
        (void)(*runner)->Run();
      },
      "Engine returned invalid run feedback");
}

TEST(EngineRunnerTest, RejectsInvalidNotificationsDuringCreation) {
  NullNotificationEngine engine;

  const absl::Status status = EngineRunner::Create(engine).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "NotificationSet received a null notification");
}

TEST(EngineRunnerTest, RejectsAConcurrentRun) {
  IdleEngine engine;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));
  engine.WaitUntilRun();

  const absl::Status status = runner->Run();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message(), "Engine runner has already started or stopped");
  runner->Stop();
  ASSERT_OK(thread.Wait());
}

TEST(EngineRunnerTest, StopBeforeRunPreventsStartup) {
  IdleEngine engine;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(engine));

  runner->Stop();
  runner->Stop();
  const absl::Status status = runner->Run();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message(), "Engine runner has already started or stopped");
}

TEST(EngineRunnerTest, DoesNotLoseAWakeBetweenIdleCheckAndWait) {
  Notification notification;
  MpscNotifyQueue<int, 1> queue(notification);
  WakeRaceEngine engine(queue, notification);
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));

  engine.WaitUntilEmptyWasChecked();
  int value = 13;
  ASSERT_TRUE(queue.TryPush(std::move(value)));
  engine.AllowIdleReturn();
  engine.WaitUntilProcessed();

  runner->Stop();
  ASSERT_OK(thread.Wait());
}

TEST(EngineRunnerTest, RechecksForWorkAfterArmingExternalNotifications) {
  ArmRaceEngine engine;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(engine));
  ASSERT_OK_AND_ASSIGN(BlockingCallbackThread thread,
                       BlockingCallbackThread::Start([&runner] { return runner->Run(); }));

  engine.WaitUntilProcessed();
  runner->Stop();
  ASSERT_OK(thread.Wait());

  EXPECT_GE(engine.arm_count(), 1);
  EXPECT_GE(engine.disarm_count(), 1);
}

}  // namespace
}  // namespace zebes
