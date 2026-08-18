#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/notification.h"

namespace zebes {

// A platform-native blocking wait set for software and external notifications.
// The supplied Notification objects and borrowed native handles must outlive it.
class NotificationSet {
 public:
  ~NotificationSet();

  NotificationSet(NotificationSet&& other) noexcept;
  NotificationSet& operator=(NotificationSet&& other) = delete;

  NotificationSet(const NotificationSet&) = delete;
  NotificationSet& operator=(const NotificationSet&) = delete;

  static absl::StatusOr<NotificationSet> Create(absl::Span<Notification* const> notifications);

  absl::Status Arm();
  void Disarm() noexcept;
  absl::Status Wait();

 private:
  class Impl;

  NotificationSet(std::vector<Notification*> notifications, std::unique_ptr<Impl> impl);

  std::vector<Notification*> notifications_;
  std::unique_ptr<Impl> impl_;
  size_t armed_count_ = 0;
};

}  // namespace zebes
