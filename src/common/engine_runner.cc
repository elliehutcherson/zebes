#include "common/engine_runner.h"

#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/notification_set.h"
#include "common/status_macros.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<EngineRunner>> EngineRunner::Create(Engine& engine) {
  std::unique_ptr<Notification> stop_notification = std::make_unique<Notification>();
  const absl::Span<Notification* const> engine_notifications = engine.Notifications();
  std::vector<Notification*> notifications(engine_notifications.begin(),
                                           engine_notifications.end());
  notifications.push_back(stop_notification.get());
  ASSIGN_OR_RETURN(NotificationSet notification_set, NotificationSet::Create(notifications));
  return std::unique_ptr<EngineRunner>(
      new EngineRunner(engine, std::move(stop_notification), std::move(notification_set)));
}

EngineRunner::EngineRunner(Engine& engine, std::unique_ptr<Notification> stop_notification,
                           NotificationSet notification_set)
    : engine_(engine),
      stop_notification_(std::move(stop_notification)),
      notification_set_(std::move(notification_set)) {}

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
    ASSIGN_OR_RETURN(const RunFeedback feedback, engine_.Run());
    if (feedback == RunFeedback::kDidWork) {
      continue;
    }
    ABSL_CHECK(feedback == RunFeedback::kIdle) << "Engine returned invalid run feedback";
    RETURN_IF_ERROR(notification_set_.Arm());
    auto disarm = absl::MakeCleanup([this] { notification_set_.Disarm(); });

    ASSIGN_OR_RETURN(const RunFeedback recheck, engine_.Run());
    if (recheck == RunFeedback::kDidWork) {
      continue;
    }
    ABSL_CHECK(recheck == RunFeedback::kIdle) << "Engine returned invalid run feedback";

    if (state_.load(std::memory_order_acquire) == State::kRunning) {
      RETURN_IF_ERROR(notification_set_.Wait());
    }
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

    if (desired == State::kStopping) stop_notification_->Notify();
    return;
  }
}

}  // namespace zebes
