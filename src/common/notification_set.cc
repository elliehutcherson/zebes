#include "common/notification_set.h"

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "common/status_macros.h"

#if defined(__linux__)
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#error "NotificationSet has no implementation for this platform"
#endif

namespace zebes {
namespace {

absl::Status PlatformError(const char* operation) {
#if defined(_WIN32)
  return absl::InternalError(
      absl::StrCat(operation, " failed with Windows error ", GetLastError()));
#else
  return absl::InternalError(absl::StrCat(operation, " failed: ", std::strerror(errno)));
#endif
}

// Time left until `deadline`, floored at zero so a deadline that has already
// passed polls once instead of waiting.
absl::Duration RemainingUntil(absl::Time deadline) {
  const absl::Duration remaining = deadline - absl::Now();
  return remaining > absl::ZeroDuration() ? remaining : absl::ZeroDuration();
}

#if defined(__linux__)
// epoll_wait's timeout is milliseconds, and -1 means no timeout. A sub-
// millisecond remainder rounds up so a deadline is never woken early.
int RemainingMilliseconds(std::optional<absl::Time> deadline) {
  if (!deadline.has_value()) return -1;
  const int64_t milliseconds = absl::ToInt64Milliseconds(
      RemainingUntil(*deadline) + absl::Milliseconds(1) - absl::Nanoseconds(1));
  if (milliseconds > INT_MAX) return INT_MAX;
  return static_cast<int>(milliseconds);
}
#elif defined(__APPLE__)
// Fills `timeout` and reports whether kevent should use it at all. A null
// timespec is kevent's spelling of "wait indefinitely"; a zeroed one polls.
bool FillTimeout(std::optional<absl::Time> deadline, struct timespec* timeout) {
  if (!deadline.has_value()) return false;
  *timeout = absl::ToTimespec(RemainingUntil(*deadline));
  return true;
}
#elif defined(_WIN32)
DWORD RemainingWindowsMilliseconds(std::optional<absl::Time> deadline) {
  if (!deadline.has_value()) return INFINITE;
  const int64_t milliseconds = absl::ToInt64Milliseconds(
      RemainingUntil(*deadline) + absl::Milliseconds(1) - absl::Nanoseconds(1));
  // INFINITE is the largest DWORD, so it must not be reachable by rounding.
  if (milliseconds >= INFINITE) return INFINITE - 1;
  return static_cast<DWORD>(milliseconds);
}
#endif

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

class NotificationSet::Impl final : public NotificationSink {
 public:
  ~Impl() override {
#if defined(__linux__)
    if (software_fd_ >= 0) {
      close(software_fd_);
    }
    if (poll_fd_ >= 0) {
      close(poll_fd_);
    }
#elif defined(__APPLE__)
    if (poll_fd_ >= 0) {
      close(poll_fd_);
    }
#elif defined(_WIN32)
    if (software_event_ != nullptr) {
      CloseHandle(software_event_);
    }
#endif
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  // Creates the wake primitive and the multiplexer. External wait handles join
  // later through Register, which is what lets an engine construct its own
  // notifications against this sink before any of its handles exist.
  static absl::StatusOr<std::unique_ptr<Impl>> Create() {
    auto impl = std::unique_ptr<Impl>(new Impl());
#if defined(__linux__)
    impl->poll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (impl->poll_fd_ < 0) {
      return PlatformError("epoll_create1");
    }

    impl->software_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (impl->software_fd_ < 0) {
      return PlatformError("eventfd");
    }

    epoll_event event = {};
    event.events = EPOLLIN;
    event.data.fd = impl->software_fd_;
    if (epoll_ctl(impl->poll_fd_, EPOLL_CTL_ADD, impl->software_fd_, &event) < 0) {
      return PlatformError("epoll_ctl for the software notification");
    }
#elif defined(__APPLE__)
    impl->poll_fd_ = kqueue();
    if (impl->poll_fd_ < 0) {
      return PlatformError("kqueue");
    }

    struct kevent change = {};
    EV_SET(&change, kSoftwareNotification, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (kevent(impl->poll_fd_, &change, 1, nullptr, 0, nullptr) < 0) {
      return PlatformError("kevent for the software notification");
    }
#elif defined(_WIN32)
    impl->software_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (impl->software_event_ == nullptr) {
      return PlatformError("CreateEvent");
    }
    impl->handles_.push_back(impl->software_event_);
#endif
    return impl;
  }

  absl::Status Register(const NativeWaitHandle& handle) {
#if defined(__linux__)
    epoll_event event = {};
    event.events = EPOLLIN;
    event.data.fd = static_cast<int>(handle.value);
    if (epoll_ctl(poll_fd_, EPOLL_CTL_ADD, event.data.fd, &event) < 0) {
      return PlatformError("epoll_ctl for an external notification");
    }
#elif defined(__APPLE__)
    struct kevent change = {};
    EV_SET(&change, static_cast<uintptr_t>(handle.value), EVFILT_READ, EV_ADD, 0, 0, nullptr);
    if (kevent(poll_fd_, &change, 1, nullptr, 0, nullptr) < 0) {
      return PlatformError("kevent for an external notification");
    }
#elif defined(_WIN32)
    if (handles_.size() >= MAXIMUM_WAIT_OBJECTS) {
      return absl::ResourceExhaustedError("NotificationSet exceeds the Windows wait-object limit");
    }
    handles_.push_back(reinterpret_cast<HANDLE>(handle.value));
#endif
    return absl::OkStatus();
  }

  void Signal() noexcept override {
#if defined(__linux__)
    constexpr uint64_t value = 1;
    ssize_t result;
    do {
      result = write(software_fd_, &value, sizeof(value));
    } while (result < 0 && errno == EINTR);
    if (result == sizeof(value) || (result < 0 && errno == EAGAIN)) {
      return;
    }
    std::abort();
#elif defined(__APPLE__)
    struct kevent change = {};
    EV_SET(&change, kSoftwareNotification, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
    int result;
    do {
      result = kevent(poll_fd_, &change, 1, nullptr, 0, nullptr);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
      std::abort();
    }
#elif defined(_WIN32)
    if (!SetEvent(software_event_)) {
      std::abort();
    }
#endif
  }

  // A timeout and a wake are the same outcome here: both return OK and leave it
  // to the caller's next poll to find out whether anything is ready.
  //
  // Every interruption recomputes the remaining time from `deadline` rather
  // than reusing the original span. Resuming with the full span would let a
  // stream of signals extend the wait without bound, which is exactly the
  // unbounded sleep a deadline exists to prevent.
  absl::Status Wait(std::optional<absl::Time> deadline) {
#if defined(__linux__)
    epoll_event event = {};
    int result;
    do {
      result = epoll_wait(poll_fd_, &event, 1, RemainingMilliseconds(deadline));
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
      return PlatformError("epoll_wait");
    }
    // Drain unconditionally. An external descriptor can win every epoll_wait,
    // and a stale software count would then wake the next wait immediately. A
    // timeout drains too: the read is non-blocking and clears a stale count.
    DrainSoftwareNotification();
#elif defined(__APPLE__)
    struct kevent event = {};
    int result;
    do {
      struct timespec timeout = {};
      const bool bounded = FillTimeout(deadline, &timeout);
      result = kevent(poll_fd_, nullptr, 0, &event, 1, bounded ? &timeout : nullptr);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
      return PlatformError("kevent wait");
    }
#elif defined(_WIN32)
    const DWORD handle_count = static_cast<DWORD>(handles_.size());
    const DWORD result = WaitForMultipleObjects(handle_count, handles_.data(), FALSE,
                                                RemainingWindowsMilliseconds(deadline));
    if (result == WAIT_FAILED) {
      return PlatformError("WaitForMultipleObjects");
    }
    // WAIT_TIMEOUT is neither a failure nor a signalled index, so it has to be
    // taken before the range check rejects it as an unexpected result.
    if (result == WAIT_TIMEOUT) {
      return absl::OkStatus();
    }
    if (result < WAIT_OBJECT_0 || result >= WAIT_OBJECT_0 + handle_count) {
      return absl::InternalError("WaitForMultipleObjects returned an unexpected result");
    }
#endif
    return absl::OkStatus();
  }

 private:
  Impl() = default;

#if defined(__linux__)
  void DrainSoftwareNotification() noexcept {
    uint64_t value;
    ssize_t result;
    do {
      do {
        result = read(software_fd_, &value, sizeof(value));
      } while (result < 0 && errno == EINTR);
    } while (result == sizeof(value));
    if (result < 0 && errno != EAGAIN) {
      std::abort();
    }
  }

  int poll_fd_ = -1;
  int software_fd_ = -1;
#elif defined(__APPLE__)
  static constexpr uintptr_t kSoftwareNotification = 1;
  int poll_fd_ = -1;
#elif defined(_WIN32)
  HANDLE software_event_ = nullptr;
  std::vector<HANDLE> handles_;
#endif
};

NotificationSet::~NotificationSet() { Disarm(); }

absl::StatusOr<std::unique_ptr<NotificationSet>> NotificationSet::Create() {
  ASSIGN_OR_RETURN(std::unique_ptr<Impl> impl, Impl::Create());
  return std::unique_ptr<NotificationSet>(new NotificationSet(std::move(impl)));
}

absl::StatusOr<Notification*> NotificationSet::AddSoftware() {
  RETURN_IF_ERROR(CheckMutable());
  notifications_.push_back(std::unique_ptr<Notification>(new Notification(*impl_)));
  return notifications_.back().get();
}

absl::StatusOr<Notification*> NotificationSet::AddSoftware(NotificationCallbacks callbacks) {
  RETURN_IF_ERROR(CheckMutable());
  notifications_.push_back(
      std::unique_ptr<Notification>(new Notification(*impl_, std::move(callbacks))));
  return notifications_.back().get();
}

absl::StatusOr<Notification*> NotificationSet::AddExternal(NativeWaitHandle native_wait_handle,
                                                           NotificationCallbacks callbacks) {
  RETURN_IF_ERROR(CheckMutable());
  RETURN_IF_ERROR(ValidateNativeWaitHandle(native_wait_handle));
  if (!registered_wait_handles_.insert(native_wait_handle.value).second) {
    return absl::InvalidArgumentError("NotificationSet received a duplicate native wait handle");
  }
  RETURN_IF_ERROR(impl_->Register(native_wait_handle));
  notifications_.push_back(std::unique_ptr<Notification>(
      new Notification(*impl_, native_wait_handle, std::move(callbacks))));
  return notifications_.back().get();
}

absl::Status NotificationSet::Seal() {
  RETURN_IF_ERROR(CheckMutable());
  if (notifications_.empty()) {
    return absl::FailedPreconditionError("NotificationSet requires at least one notification");
  }
  sealed_ = true;
  return absl::OkStatus();
}

absl::Status NotificationSet::Arm() {
  if (armed_count_ != 0) {
    return absl::FailedPreconditionError("NotificationSet is already armed");
  }
  for (const std::unique_ptr<Notification>& notification : notifications_) {
    absl::Status status = notification->Arm();
    if (!status.ok()) {
      Disarm();
      return status;
    }
    ++armed_count_;
  }
  // Published last, so a producer never sees the set armed while a source it
  // owns is not. The fence inside makes the caller's recheck pass visible to
  // any producer that reads the flag as false.
  impl_->SetArmed(true);
  return absl::OkStatus();
}

void NotificationSet::Disarm() noexcept {
  // Cleared first: a producer that reads the flag after this point pays no
  // syscall, and the runner is awake and about to poll for its work anyway.
  impl_->SetArmed(false);
  while (armed_count_ > 0) {
    --armed_count_;
    notifications_[armed_count_]->Disarm();
  }
}

absl::Status NotificationSet::Wait() { return WaitInternal(std::nullopt); }

absl::Status NotificationSet::WaitUntil(absl::Time deadline) { return WaitInternal(deadline); }

absl::Status NotificationSet::WaitInternal(std::optional<absl::Time> deadline) {
  if (notifications_.empty() || armed_count_ != notifications_.size()) {
    return absl::FailedPreconditionError("NotificationSet must be armed before waiting");
  }
  return impl_->Wait(deadline);
}

absl::Status NotificationSet::CheckMutable() const {
  if (sealed_) {
    return absl::FailedPreconditionError("NotificationSet is sealed");
  }
  return absl::OkStatus();
}

NotificationSet::NotificationSet(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

}  // namespace zebes
