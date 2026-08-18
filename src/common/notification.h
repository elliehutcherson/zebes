#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace zebes {

class NotificationSink {
 public:
  virtual ~NotificationSink() = default;
  virtual void Signal() noexcept = 0;
};

enum class NativeWaitHandleType {
  kFileDescriptor,
  kWindowsHandle,
};

// A borrowed platform wait handle. Its owner must keep it valid while the
// containing Notification belongs to a running NotificationSet.
struct NativeWaitHandle {
  NativeWaitHandleType type;
  intptr_t value;
};

class NotificationCallbacks {
 public:
  using ArmCallback = absl::AnyInvocable<absl::Status()>;
  using DisarmCallback = absl::AnyInvocable<void() noexcept>;

  NotificationCallbacks(ArmCallback arm, DisarmCallback disarm);

  absl::Status Arm();
  void Disarm() noexcept;

 private:
  ArmCallback arm_;
  DisarmCallback disarm_;
};

struct ExternalNotificationOptions {
  NativeWaitHandle wait_handle;
  NotificationCallbacks callbacks;
};

// A logical wake source consumed through NotificationSet.
//
// A default-constructed Notification is a software source signaled by Notify.
// External sources carry a borrowed native wait handle and arm/disarm callbacks.
// NotificationSet owns the platform-specific multiplexing objects.
class Notification {
 public:
  static_assert(std::atomic<NotificationSink*>::is_always_lock_free,
                "Notification delivery requires an always-lock-free atomic pointer");
  static_assert(std::atomic<size_t>::is_always_lock_free,
                "Notification delivery requires an always-lock-free notifier count");
  static_assert(std::atomic<bool>::is_always_lock_free,
                "Notification delivery requires an always-lock-free pending flag");

  Notification();
  explicit Notification(NotificationCallbacks callbacks);

  static absl::StatusOr<std::unique_ptr<Notification>> CreateExternal(
      ExternalNotificationOptions options);

  Notification(const Notification&) = delete;
  Notification& operator=(const Notification&) = delete;

  void Notify() noexcept;
  absl::Status Arm();
  void Disarm() noexcept;

  bool IsExternal() const { return native_wait_handle_.has_value(); }
  const NativeWaitHandle* native_wait_handle() const {
    return native_wait_handle_ ? &*native_wait_handle_ : nullptr;
  }

  // NotificationSet uses this setup-time attachment. Producers must be stopped
  // before the set detaches or either object is destroyed.
  absl::Status Attach(NotificationSink& sink);
  void Detach(NotificationSink& sink) noexcept;

 private:
  Notification(NotificationCallbacks callbacks, NativeWaitHandle native_wait_handle);

  NotificationCallbacks callbacks_;
  std::optional<NativeWaitHandle> native_wait_handle_;
  std::atomic<NotificationSink*> sink_ = nullptr;
  std::atomic<bool> pending_before_attach_ = false;
  // Detach waits for readers that observed the old sink before releasing it.
  std::atomic<size_t> active_notifiers_ = 0;
};

}  // namespace zebes
