#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace zebes {

// The wake target every producer shares: a native wake primitive plus the flag
// that says whether a thread is parked on it. NotificationSet implements it and
// owns the arm state; a Notification holds one for its whole lifetime.
class NotificationSink {
 public:
  static_assert(std::atomic<bool>::is_always_lock_free,
                "Notification delivery requires an always-lock-free arm flag");

  virtual ~NotificationSink() = default;

  // Wakes a parked waiter, and does nothing when none is parked.
  //
  // The caller must have published its work before calling this. The seq_cst
  // fence pairs with the one in SetArmed: this is a store-then-load on both
  // sides, so if this load reads false, the waiter had not armed yet and its
  // recheck pass is guaranteed to observe the published work. Neither fence
  // may be weakened, and neither side may reorder its publish past its load.
  void SignalIfArmed() noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (!armed_.load(std::memory_order_acquire)) {
      return;
    }
    Signal();
  }

  // Called only by the owning set, from the thread that blocks in Wait.
  void SetArmed(bool armed) noexcept {
    armed_.store(armed, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

 protected:
  virtual void Signal() noexcept = 0;

 private:
  std::atomic<bool> armed_ = false;
};

enum class NativeWaitHandleType {
  kFileDescriptor,
  kWindowsHandle,
};

// A borrowed platform wait handle. Its owner must keep it valid and open for
// the lifetime of the set the notification belongs to.
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

// A logical wake source owned by a NotificationSet.
//
// A software source is signaled by Notify. An external source is signaled by
// its native wait handle; its arm/disarm callbacks bracket each blocking wait.
//
// The sink is bound at construction and never changes, so Notify is correct
// from any thread at any point in the set's lifetime.
class Notification {
 public:
  Notification(const Notification&) = delete;
  Notification& operator=(const Notification&) = delete;

  // Publish the work first, then call this. Notify costs a syscall only when a
  // thread is actually parked; otherwise it is a fence and a load, and delivery
  // falls to the waiter's recheck pass. A producer that notifies before it
  // publishes breaks that guarantee and can hang the waiter.
  void Notify() noexcept { sink_.SignalIfArmed(); }

  bool IsExternal() const { return native_wait_handle_.has_value(); }
  const NativeWaitHandle* native_wait_handle() const {
    return native_wait_handle_ ? &*native_wait_handle_ : nullptr;
  }

 private:
  friend class NotificationSet;

  explicit Notification(NotificationSink& sink);
  Notification(NotificationSink& sink, NotificationCallbacks callbacks);
  Notification(NotificationSink& sink, NativeWaitHandle native_wait_handle,
               NotificationCallbacks callbacks);

  absl::Status Arm();
  void Disarm() noexcept;

  NotificationSink& sink_;
  NotificationCallbacks callbacks_;
  std::optional<NativeWaitHandle> native_wait_handle_;
};

}  // namespace zebes
