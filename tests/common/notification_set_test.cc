#include "common/notification_set.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "common/blocking_callback_thread.h"
#include "common/notification.h"
#include "gtest/gtest.h"
#include "macros.h"

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zebes {
namespace {

void ConstructWithEmptyArmCallback() {
  NotificationCallbacks callbacks(NotificationCallbacks::ArmCallback{}, []() noexcept {});
  (void)callbacks;
}

void ConstructWithEmptyDisarmCallback() {
  NotificationCallbacks callbacks([] { return absl::OkStatus(); },
                                  NotificationCallbacks::DisarmCallback{});
  (void)callbacks;
}

TEST(NotificationCallbacksTest, RejectsAnEmptyArmCallback) {
  EXPECT_DEATH(ConstructWithEmptyArmCallback(), "arm callback must not be empty");
}

TEST(NotificationCallbacksTest, RejectsAnEmptyDisarmCallback) {
  EXPECT_DEATH(ConstructWithEmptyDisarmCallback(), "disarm callback must not be empty");
}

TEST(NotificationSetTest, CoalescesSoftwareNotificationsIntoANativeWait) {
  Notification first;
  Notification second;
  std::array<Notification*, 2> notifications = {&first, &second};
  ASSERT_OK_AND_ASSIGN(NotificationSet notification_set, NotificationSet::Create(notifications));
  ASSERT_OK(notification_set.Arm());

  first.Notify();
  second.Notify();

  EXPECT_TRUE(notification_set.Wait().ok());
  notification_set.Disarm();
}

TEST(NotificationSetTest, RejectsAnEmptySet) {
  const absl::Span<Notification* const> notifications;

  const absl::Status status = NotificationSet::Create(notifications).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "NotificationSet requires at least one notification");
}

TEST(NotificationSetTest, RejectsANullNotification) {
  std::array<Notification*, 1> notifications = {nullptr};

  const absl::Status status = NotificationSet::Create(notifications).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "NotificationSet received a null notification");
}

TEST(NotificationSetTest, RejectsADuplicateNotification) {
  Notification notification;
  std::array<Notification*, 2> notifications = {&notification, &notification};

  const absl::Status status = NotificationSet::Create(notifications).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "NotificationSet received a duplicate notification");
}

TEST(NotificationSetTest, RejectsANotificationAlreadyOwnedByAnotherSet) {
  Notification notification;
  std::array<Notification*, 1> notifications = {&notification};
  ASSERT_OK_AND_ASSIGN(NotificationSet first_set, NotificationSet::Create(notifications));

  const absl::Status status = NotificationSet::Create(notifications).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message(), "Notification already belongs to a notification set");
}

TEST(NotificationSetTest, EnforcesArmAndWaitOrder) {
  Notification notification;
  std::array<Notification*, 1> notifications = {&notification};
  ASSERT_OK_AND_ASSIGN(NotificationSet notification_set, NotificationSet::Create(notifications));

  const absl::Status wait_status = notification_set.Wait();
  EXPECT_EQ(wait_status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(wait_status.message(), "NotificationSet must be armed before waiting");

  ASSERT_OK(notification_set.Arm());
  const absl::Status arm_status = notification_set.Arm();
  EXPECT_EQ(arm_status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(arm_status.message(), "NotificationSet is already armed");
  notification_set.Disarm();
}

TEST(NotificationSetTest, RollsBackAlreadyArmedSourcesWhenArmingFails) {
  std::atomic<bool> first_disarmed = false;
  Notification first(NotificationCallbacks(
      [] { return absl::OkStatus(); },
      [&first_disarmed]() noexcept { first_disarmed.store(true, std::memory_order_release); }));
  Notification second(NotificationCallbacks(
      [] { return absl::DataLossError("could not arm interrupt"); }, []() noexcept {}));
  std::array<Notification*, 2> notifications = {&first, &second};
  ASSERT_OK_AND_ASSIGN(NotificationSet notification_set, NotificationSet::Create(notifications));

  const absl::Status status = notification_set.Arm();

  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(status.message(), "could not arm interrupt");
  EXPECT_TRUE(first_disarmed.load(std::memory_order_acquire));
}

#if defined(__linux__) || defined(__APPLE__)
TEST(NotificationTest, RejectsAnInvalidExternalFileDescriptor) {
  ExternalNotificationOptions options{
      .wait_handle =
          NativeWaitHandle{
              .type = NativeWaitHandleType::kFileDescriptor,
              .value = -1,
          },
      .callbacks = NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {}),
  };

  const absl::Status status = Notification::CreateExternal(std::move(options)).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "External notification file descriptor is invalid");
}

TEST(NotificationSetTest, RejectsADuplicateExternalFileDescriptor) {
  std::array<int, 2> pipe_fds = {-1, -1};
  ASSERT_EQ(pipe(pipe_fds.data()), 0);

  ExternalNotificationOptions first_options{
      .wait_handle =
          NativeWaitHandle{
              .type = NativeWaitHandleType::kFileDescriptor,
              .value = pipe_fds[0],
          },
      .callbacks = NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {}),
  };
  ExternalNotificationOptions second_options{
      .wait_handle = first_options.wait_handle,
      .callbacks = NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {}),
  };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Notification> first,
                       Notification::CreateExternal(std::move(first_options)));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Notification> second,
                       Notification::CreateExternal(std::move(second_options)));
  std::array<Notification*, 2> notifications = {first.get(), second.get()};

  const absl::Status status = NotificationSet::Create(notifications).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "NotificationSet received a duplicate native wait handle");
  EXPECT_EQ(close(pipe_fds[0]), 0);
  EXPECT_EQ(close(pipe_fds[1]), 0);
}

TEST(NotificationSetTest, WaitsForAnExternalFileDescriptor) {
  std::array<int, 2> pipe_fds = {-1, -1};
  ASSERT_EQ(pipe(pipe_fds.data()), 0);

  {
    ExternalNotificationOptions options{
        .wait_handle =
            NativeWaitHandle{
                .type = NativeWaitHandleType::kFileDescriptor,
                .value = pipe_fds[0],
            },
        .callbacks = NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {}),
    };
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Notification> notification,
                         Notification::CreateExternal(std::move(options)));
    std::array<Notification*, 1> notifications = {notification.get()};
    ASSERT_OK_AND_ASSIGN(NotificationSet notification_set, NotificationSet::Create(notifications));
    ASSERT_OK(notification_set.Arm());
    ASSERT_OK_AND_ASSIGN(
        BlockingCallbackThread waiter,
        BlockingCallbackThread::Start([&notification_set] { return notification_set.Wait(); }));

    constexpr uint8_t byte = 1;
    ASSERT_EQ(write(pipe_fds[1], &byte, sizeof(byte)), sizeof(byte));
    ASSERT_OK(waiter.Wait());
    notification_set.Disarm();
  }

  EXPECT_EQ(close(pipe_fds[0]), 0);
  EXPECT_EQ(close(pipe_fds[1]), 0);
}
#elif defined(_WIN32)
TEST(NotificationTest, RejectsAnInvalidExternalWindowsHandle) {
  ExternalNotificationOptions options{
      .wait_handle =
          NativeWaitHandle{
              .type = NativeWaitHandleType::kWindowsHandle,
              .value = 0,
          },
      .callbacks = NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {}),
  };

  const absl::Status status = Notification::CreateExternal(std::move(options)).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "External notification Windows handle is invalid");
}

TEST(NotificationSetTest, WaitsForAnExternalWindowsEvent) {
  HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);

  {
    ExternalNotificationOptions options{
        .wait_handle =
            NativeWaitHandle{
                .type = NativeWaitHandleType::kWindowsHandle,
                .value = reinterpret_cast<intptr_t>(event),
            },
        .callbacks = NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {}),
    };
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Notification> notification,
                         Notification::CreateExternal(std::move(options)));
    std::array<Notification*, 1> notifications = {notification.get()};
    ASSERT_OK_AND_ASSIGN(NotificationSet notification_set, NotificationSet::Create(notifications));
    ASSERT_OK(notification_set.Arm());
    ASSERT_OK_AND_ASSIGN(
        BlockingCallbackThread waiter,
        BlockingCallbackThread::Start([&notification_set] { return notification_set.Wait(); }));

    ASSERT_TRUE(SetEvent(event));
    ASSERT_OK(waiter.Wait());
    notification_set.Disarm();
  }

  EXPECT_TRUE(CloseHandle(event));
}
#endif

}  // namespace
}  // namespace zebes
