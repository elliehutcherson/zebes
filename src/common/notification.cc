#include "common/notification.h"

#include <climits>
#include <memory>
#include <utility>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

absl::Status ValidateNativeWaitHandle(const NativeWaitHandle& handle) {
#if defined(_WIN32)
  if (handle.type != NativeWaitHandleType::kWindowsHandle || handle.value == 0 ||
      handle.value == -1) {
    return absl::InvalidArgumentError("External notification Windows handle is invalid");
  }
#else
  if (handle.type != NativeWaitHandleType::kFileDescriptor || handle.value < 0 ||
      handle.value > INT_MAX) {
    return absl::InvalidArgumentError("External notification file descriptor is invalid");
  }
#endif
  return absl::OkStatus();
}

}  // namespace

NotificationCallbacks::NotificationCallbacks(ArmCallback arm, DisarmCallback disarm)
    : arm_(std::move(arm)), disarm_(std::move(disarm)) {
  ABSL_CHECK(arm_) << "Notification arm callback must not be empty";
  ABSL_CHECK(disarm_) << "Notification disarm callback must not be empty";
}

absl::Status NotificationCallbacks::Arm() { return arm_(); }

void NotificationCallbacks::Disarm() noexcept { disarm_(); }

Notification::Notification()
    : Notification(NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {})) {}

Notification::Notification(NotificationCallbacks callbacks) : callbacks_(std::move(callbacks)) {}

absl::StatusOr<std::unique_ptr<Notification>> Notification::CreateExternal(
    ExternalNotificationOptions options) {
  RETURN_IF_ERROR(ValidateNativeWaitHandle(options.wait_handle));
  return std::unique_ptr<Notification>(
      new Notification(std::move(options.callbacks), options.wait_handle));
}

void Notification::Notify() noexcept {
  active_notifiers_.fetch_add(1);
  NotificationSink* sink = sink_.load();
  if (sink != nullptr) {
    sink->Signal();
  } else {
    pending_before_attach_.store(true, std::memory_order_release);
    sink = sink_.load();
    if (sink != nullptr && pending_before_attach_.exchange(false, std::memory_order_acq_rel)) {
      sink->Signal();
    }
  }
  if (active_notifiers_.fetch_sub(1) == 1) {
    active_notifiers_.notify_all();
  }
}

absl::Status Notification::Arm() { return callbacks_.Arm(); }

void Notification::Disarm() noexcept { callbacks_.Disarm(); }

absl::Status Notification::Attach(NotificationSink& sink) {
  NotificationSink* expected = nullptr;
  if (!sink_.compare_exchange_strong(expected, &sink, std::memory_order_acq_rel,
                                     std::memory_order_acquire)) {
    return absl::FailedPreconditionError("Notification already belongs to a notification set");
  }
  if (pending_before_attach_.exchange(false, std::memory_order_acq_rel)) {
    sink.Signal();
  }
  return absl::OkStatus();
}

void Notification::Detach(NotificationSink& sink) noexcept {
  NotificationSink* expected = &sink;
  ABSL_CHECK(sink_.compare_exchange_strong(expected, nullptr))
      << "Notification detached from the wrong notification set";
  size_t active_notifiers = active_notifiers_.load();
  while (active_notifiers != 0) {
    active_notifiers_.wait(active_notifiers, std::memory_order_acquire);
    active_notifiers = active_notifiers_.load();
  }
}

Notification::Notification(NotificationCallbacks callbacks, NativeWaitHandle native_wait_handle)
    : callbacks_(std::move(callbacks)), native_wait_handle_(native_wait_handle) {}

}  // namespace zebes
