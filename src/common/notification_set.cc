#include "common/notification_set.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
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

  static absl::StatusOr<std::unique_ptr<Impl>> Create(
      absl::Span<Notification* const> notifications) {
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

    for (Notification* notification : notifications) {
      if (!notification->IsExternal()) {
        continue;
      }
      const NativeWaitHandle& handle = *notification->native_wait_handle();
      event = {};
      event.events = EPOLLIN;
      event.data.fd = static_cast<int>(handle.value);
      if (epoll_ctl(impl->poll_fd_, EPOLL_CTL_ADD, event.data.fd, &event) < 0) {
        return PlatformError("epoll_ctl for an external notification");
      }
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

    for (Notification* notification : notifications) {
      if (!notification->IsExternal()) {
        continue;
      }
      const NativeWaitHandle& handle = *notification->native_wait_handle();
      EV_SET(&change, static_cast<uintptr_t>(handle.value), EVFILT_READ, EV_ADD, 0, 0, nullptr);
      if (kevent(impl->poll_fd_, &change, 1, nullptr, 0, nullptr) < 0) {
        return PlatformError("kevent for an external notification");
      }
    }
#elif defined(_WIN32)
    impl->software_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (impl->software_event_ == nullptr) {
      return PlatformError("CreateEvent");
    }
    impl->handles_.push_back(impl->software_event_);

    for (Notification* notification : notifications) {
      if (!notification->IsExternal()) {
        continue;
      }
      const NativeWaitHandle& handle = *notification->native_wait_handle();
      impl->handles_.push_back(reinterpret_cast<HANDLE>(handle.value));
    }
    if (impl->handles_.size() > MAXIMUM_WAIT_OBJECTS) {
      return absl::ResourceExhaustedError("NotificationSet exceeds the Windows wait-object limit");
    }
#endif
    return impl;
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

  absl::Status Wait() {
#if defined(__linux__)
    epoll_event event = {};
    int result;
    do {
      result = epoll_wait(poll_fd_, &event, 1, -1);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
      return PlatformError("epoll_wait");
    }
    if (event.data.fd == software_fd_) {
      DrainSoftwareNotification();
    }
#elif defined(__APPLE__)
    struct kevent event = {};
    int result;
    do {
      result = kevent(poll_fd_, nullptr, 0, &event, 1, nullptr);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
      return PlatformError("kevent wait");
    }
#elif defined(_WIN32)
    const DWORD handle_count = static_cast<DWORD>(handles_.size());
    const DWORD result = WaitForMultipleObjects(handle_count, handles_.data(), FALSE, INFINITE);
    if (result == WAIT_FAILED) {
      return PlatformError("WaitForMultipleObjects");
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

NotificationSet::~NotificationSet() {
  if (impl_ == nullptr) {
    return;
  }
  Disarm();
  for (Notification* notification : notifications_) {
    notification->Detach(*impl_);
  }
}

NotificationSet::NotificationSet(NotificationSet&& other) noexcept
    : notifications_(std::move(other.notifications_)),
      impl_(std::move(other.impl_)),
      armed_count_(other.armed_count_) {
  other.armed_count_ = 0;
}

absl::StatusOr<NotificationSet> NotificationSet::Create(
    absl::Span<Notification* const> notifications) {
  if (notifications.empty()) {
    return absl::InvalidArgumentError("NotificationSet requires at least one notification");
  }

  std::vector<Notification*> owned_notifications;
  owned_notifications.reserve(notifications.size());
  absl::flat_hash_set<Notification*> seen_notifications;
  absl::flat_hash_set<intptr_t> seen_native_wait_handles;
  for (Notification* notification : notifications) {
    if (notification == nullptr) {
      return absl::InvalidArgumentError("NotificationSet received a null notification");
    }
    if (!seen_notifications.insert(notification).second) {
      return absl::InvalidArgumentError("NotificationSet received a duplicate notification");
    }
    if (notification->IsExternal() &&
        !seen_native_wait_handles.insert(notification->native_wait_handle()->value).second) {
      return absl::InvalidArgumentError("NotificationSet received a duplicate native wait handle");
    }
    owned_notifications.push_back(notification);
  }

  ASSIGN_OR_RETURN(std::unique_ptr<Impl> impl, Impl::Create(owned_notifications));

  size_t attached_count = 0;
  for (Notification* notification : owned_notifications) {
    absl::Status status = notification->Attach(*impl);
    if (!status.ok()) {
      for (size_t i = 0; i < attached_count; ++i) {
        owned_notifications[i]->Detach(*impl);
      }
      return status;
    }
    ++attached_count;
  }
  return NotificationSet(std::move(owned_notifications), std::move(impl));
}

absl::Status NotificationSet::Arm() {
  if (armed_count_ != 0) {
    return absl::FailedPreconditionError("NotificationSet is already armed");
  }
  for (Notification* notification : notifications_) {
    absl::Status status = notification->Arm();
    if (!status.ok()) {
      Disarm();
      return status;
    }
    ++armed_count_;
  }
  return absl::OkStatus();
}

void NotificationSet::Disarm() noexcept {
  while (armed_count_ > 0) {
    --armed_count_;
    notifications_[armed_count_]->Disarm();
  }
}

absl::Status NotificationSet::Wait() {
  if (armed_count_ != notifications_.size()) {
    return absl::FailedPreconditionError("NotificationSet must be armed before waiting");
  }
  return impl_->Wait();
}

NotificationSet::NotificationSet(std::vector<Notification*> notifications,
                                 std::unique_ptr<Impl> impl)
    : notifications_(std::move(notifications)), impl_(std::move(impl)) {}

}  // namespace zebes
