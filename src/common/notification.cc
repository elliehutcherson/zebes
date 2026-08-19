#include "common/notification.h"

#include <utility>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"

namespace zebes {

NotificationCallbacks::NotificationCallbacks(ArmCallback arm, DisarmCallback disarm)
    : arm_(std::move(arm)), disarm_(std::move(disarm)) {
  ABSL_CHECK(arm_) << "Notification arm callback must not be empty";
  ABSL_CHECK(disarm_) << "Notification disarm callback must not be empty";
}

absl::Status NotificationCallbacks::Arm() { return arm_(); }

void NotificationCallbacks::Disarm() noexcept { disarm_(); }

Notification::Notification(NotificationSink& sink)
    : Notification(sink, NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {})) {
}

Notification::Notification(NotificationSink& sink, NotificationCallbacks callbacks)
    : sink_(sink), callbacks_(std::move(callbacks)) {}

Notification::Notification(NotificationSink& sink, NativeWaitHandle native_wait_handle,
                           NotificationCallbacks callbacks)
    : sink_(sink), callbacks_(std::move(callbacks)), native_wait_handle_(native_wait_handle) {}

absl::Status Notification::Arm() { return callbacks_.Arm(); }

void Notification::Disarm() noexcept { callbacks_.Disarm(); }

}  // namespace zebes
