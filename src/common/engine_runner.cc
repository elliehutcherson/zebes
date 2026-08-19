#include "common/engine_runner.h"

#include "absl/cleanup/cleanup.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/notification.h"
#include "common/notification_set.h"
#include "common/status_macros.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<EngineRunner>> EngineRunner::Create(Engine& engine) {
  NotificationSet& notification_set = engine.notification_set();
  ASSIGN_OR_RETURN(Notification * stop_notification, notification_set.AddSoftware());
  RETURN_IF_ERROR(notification_set.Seal());
  return std::unique_ptr<EngineRunner>(
      new EngineRunner(engine, notification_set, *stop_notification));
}

EngineRunner::EngineRunner(Engine& engine, NotificationSet& notification_set,
                           Notification& stop_notification)
    : engine_(engine), notification_set_(notification_set), stop_notification_(stop_notification) {}

EngineRunner::~EngineRunner() {
  const State state = state_.load(std::memory_order_acquire);
  ABSL_CHECK(state == State::kReady || state == State::kStopped)
      << "Engine runner must finish before destruction";
}

absl::Status EngineRunner::Run() {
  State expected = State::kReady;
  if (!state_.compare_exchange_strong(expected, State::kRunning, std::memory_order_acq_rel,
                                      std::memory_order_acquire)) {
    return absl::FailedPreconditionError("Engine runner has already started or stopped");
  }

  const absl::Status status = RunLoop();
  state_.store(State::kStopped, std::memory_order_release);
  return status;
}

absl::Status EngineRunner::RunLoop() {
  while (state_.load(std::memory_order_acquire) == State::kRunning) {
    ASSIGN_OR_RETURN(const RunResult result, engine_.Run());
    if (result.feedback == RunFeedback::kDidWork) {
      continue;
    }
    ABSL_CHECK(result.feedback == RunFeedback::kIdle) << "Engine returned invalid run feedback";
    RETURN_IF_ERROR(notification_set_.Arm());
    auto disarm = absl::MakeCleanup([this] { notification_set_.Disarm(); });

    // Everything below the arm is a recheck, and both halves are mandatory.
    // A producer that saw the set unarmed sent no wake at all, so this pass is
    // the only thing that delivers its work; the same holds for a hardware
    // source whose interrupt was armed a moment ago. The state load is the
    // recheck for Stop, which likewise skips its wake when the set is unarmed.
    ASSIGN_OR_RETURN(const RunResult recheck, engine_.Run());
    if (recheck.feedback == RunFeedback::kDidWork) {
      continue;
    }
    ABSL_CHECK(recheck.feedback == RunFeedback::kIdle) << "Engine returned invalid run feedback";

    if (state_.load(std::memory_order_acquire) != State::kRunning) {
      continue;
    }
    // The recheck's deadline, not the first pass's: it is the later observation
    // and so the only one that accounts for time the first pass spent running.
    if (!recheck.wake_deadline.has_value()) {
      RETURN_IF_ERROR(notification_set_.Wait());
      continue;
    }
    RETURN_IF_ERROR(notification_set_.WaitUntil(*recheck.wake_deadline));
  }

  return absl::OkStatus();
}

void EngineRunner::Stop() noexcept {
  State state = state_.load(std::memory_order_acquire);
  while (state == State::kReady || state == State::kRunning) {
    const State desired = state == State::kReady ? State::kStopped : State::kStopping;
    if (!state_.compare_exchange_weak(state, desired, std::memory_order_acq_rel,
                                      std::memory_order_acquire)) {
      continue;
    }

    // The state change is published by the compare-exchange above before Notify
    // reads the arm flag, which is the same store-then-load handshake a queue
    // producer uses. RunLoop's state load after arming is the matching recheck.
    if (desired == State::kStopping) stop_notification_.Notify();
    return;
  }
}

}  // namespace zebes
