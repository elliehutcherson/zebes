#include "common/notification_set.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
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
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());
  ASSERT_OK_AND_ASSIGN(Notification * first, notification_set->AddSoftware());
  ASSERT_OK_AND_ASSIGN(Notification * second, notification_set->AddSoftware());
  ASSERT_OK(notification_set->Seal());
  ASSERT_OK(notification_set->Arm());

  first->Notify();
  second->Notify();

  EXPECT_TRUE(notification_set->Wait().ok());
  notification_set->Disarm();
}

// Disarming drops the arm flag, so the middle Notify costs no syscall and
// leaves nothing latched. The next armed Notify still wakes the waiter.
TEST(NotificationSetTest, WakesAgainAfterADisarmAndRearm) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());
  ASSERT_OK_AND_ASSIGN(Notification * notification, notification_set->AddSoftware());
  ASSERT_OK(notification_set->Seal());

  ASSERT_OK(notification_set->Arm());
  notification->Notify();
  ASSERT_OK(notification_set->Wait());
  notification_set->Disarm();

  notification->Notify();

  ASSERT_OK(notification_set->Arm());
  notification->Notify();
  EXPECT_TRUE(notification_set->Wait().ok());
  notification_set->Disarm();
}

// The timeout is a bound on sleeping, not a source: nothing fired, so WaitUntil
// reports OK and leaves it to the caller's next poll to find that out.
TEST(NotificationSetTest, WaitUntilReturnsWhenTheDeadlinePasses) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());
  ASSERT_OK(notification_set->AddSoftware().status());
  ASSERT_OK(notification_set->Seal());
  ASSERT_OK(notification_set->Arm());

  constexpr absl::Duration kTimeout = absl::Milliseconds(50);
  const absl::Time started = absl::Now();
  EXPECT_TRUE(notification_set->WaitUntil(started + kTimeout).ok());
  const absl::Duration elapsed = absl::Now() - started;
  notification_set->Disarm();

  // Only a lower bound. Timer granularity and scheduling decide how much later
  // than the deadline the wait actually returns, and no upper bound on that is
  // true on a loaded machine.
  EXPECT_GE(elapsed, kTimeout);
}

TEST(NotificationSetTest, WaitUntilPollsOnceForADeadlineAlreadyPassed) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());
  ASSERT_OK(notification_set->AddSoftware().status());
  ASSERT_OK(notification_set->Seal());
  ASSERT_OK(notification_set->Arm());

  EXPECT_TRUE(notification_set->WaitUntil(absl::Now() - absl::Seconds(30)).ok());
  notification_set->Disarm();
}

// A deadline no test could outlast, so returning at all proves the notification
// woke the wait rather than the timeout expiring.
TEST(NotificationSetTest, WaitUntilReturnsEarlyWhenNotified) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());
  ASSERT_OK_AND_ASSIGN(Notification * notification, notification_set->AddSoftware());
  ASSERT_OK(notification_set->Seal());
  ASSERT_OK(notification_set->Arm());

  notification->Notify();

  EXPECT_TRUE(notification_set->WaitUntil(absl::Now() + absl::Hours(1)).ok());
  notification_set->Disarm();
}

TEST(NotificationSetTest, WaitUntilRequiresAnArmedSet) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());
  ASSERT_OK(notification_set->AddSoftware().status());
  ASSERT_OK(notification_set->Seal());

  const absl::Status status = notification_set->WaitUntil(absl::Now() + absl::Milliseconds(10));

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message(), "NotificationSet must be armed before waiting");
}

TEST(NotificationSetTest, RejectsSealingASetWithNoSources) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());

  const absl::Status status = notification_set->Seal();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(status.message(), "NotificationSet requires at least one notification");
}

TEST(NotificationSetTest, RejectsSourcesAddedAfterSealing) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());
  ASSERT_OK(notification_set->AddSoftware().status());
  ASSERT_OK(notification_set->Seal());

  const absl::Status add_status = notification_set->AddSoftware().status();
  EXPECT_EQ(add_status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(add_status.message(), "NotificationSet is sealed");

  const absl::Status seal_status = notification_set->Seal();
  EXPECT_EQ(seal_status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(seal_status.message(), "NotificationSet is sealed");
}

TEST(NotificationSetTest, EnforcesArmAndWaitOrder) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());
  ASSERT_OK(notification_set->AddSoftware().status());
  ASSERT_OK(notification_set->Seal());

  const absl::Status wait_status = notification_set->Wait();
  EXPECT_EQ(wait_status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(wait_status.message(), "NotificationSet must be armed before waiting");

  ASSERT_OK(notification_set->Arm());
  const absl::Status arm_status = notification_set->Arm();
  EXPECT_EQ(arm_status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(arm_status.message(), "NotificationSet is already armed");
  notification_set->Disarm();
}

TEST(NotificationSetTest, RollsBackAlreadyArmedSourcesWhenArmingFails) {
  std::atomic<bool> first_disarmed = false;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());
  ASSERT_OK(notification_set
                ->AddSoftware(NotificationCallbacks([] { return absl::OkStatus(); },
                                                    [&first_disarmed]() noexcept {
                                                      first_disarmed.store(
                                                          true, std::memory_order_release);
                                                    }))
                .status());
  ASSERT_OK(
      notification_set
          ->AddSoftware(NotificationCallbacks(
              [] { return absl::DataLossError("could not arm interrupt"); }, []() noexcept {}))
          .status());
  ASSERT_OK(notification_set->Seal());

  const absl::Status status = notification_set->Arm();

  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(status.message(), "could not arm interrupt");
  EXPECT_TRUE(first_disarmed.load(std::memory_order_acquire));
}

#if defined(__linux__) || defined(__APPLE__)
TEST(NotificationSetTest, RejectsAnInvalidExternalFileDescriptor) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());

  const absl::Status status =
      notification_set
          ->AddExternal(
              NativeWaitHandle{.type = NativeWaitHandleType::kFileDescriptor, .value = -1},
              NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {}))
          .status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "External notification file descriptor is invalid");
}

TEST(NotificationSetTest, RejectsADuplicateExternalFileDescriptor) {
  std::array<int, 2> pipe_fds = {-1, -1};
  ASSERT_EQ(pipe(pipe_fds.data()), 0);
  const NativeWaitHandle handle{.type = NativeWaitHandleType::kFileDescriptor,
                                .value = pipe_fds[0]};

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());
  ASSERT_OK(notification_set
                ->AddExternal(handle, NotificationCallbacks([] { return absl::OkStatus(); },
                                                            []() noexcept {}))
                .status());

  const absl::Status status =
      notification_set
          ->AddExternal(handle,
                        NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {}))
          .status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "NotificationSet received a duplicate native wait handle");
  EXPECT_EQ(close(pipe_fds[0]), 0);
  EXPECT_EQ(close(pipe_fds[1]), 0);
}

TEST(NotificationSetTest, WaitsForAnExternalFileDescriptor) {
  std::array<int, 2> pipe_fds = {-1, -1};
  ASSERT_EQ(pipe(pipe_fds.data()), 0);

  {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                         NotificationSet::Create());
    ASSERT_OK(
        notification_set
            ->AddExternal(NativeWaitHandle{.type = NativeWaitHandleType::kFileDescriptor,
                                           .value = pipe_fds[0]},
                          NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {}))
            .status());
    ASSERT_OK(notification_set->Seal());
    ASSERT_OK(notification_set->Arm());
    ASSERT_OK_AND_ASSIGN(
        BlockingCallbackThread waiter,
        BlockingCallbackThread::Start([&notification_set] { return notification_set->Wait(); }));

    constexpr uint8_t kByte = 1;
    ASSERT_EQ(write(pipe_fds[1], &kByte, sizeof(kByte)), sizeof(kByte));
    ASSERT_OK(waiter.Wait());
    notification_set->Disarm();
  }

  EXPECT_EQ(close(pipe_fds[0]), 0);
  EXPECT_EQ(close(pipe_fds[1]), 0);
}
#elif defined(_WIN32)
TEST(NotificationSetTest, RejectsAnInvalidExternalWindowsHandle) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                       NotificationSet::Create());

  const absl::Status status =
      notification_set
          ->AddExternal(NativeWaitHandle{.type = NativeWaitHandleType::kWindowsHandle, .value = 0},
                        NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {}))
          .status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "External notification Windows handle is invalid");
}

TEST(NotificationSetTest, WaitsForAnExternalWindowsEvent) {
  HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);

  {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<NotificationSet> notification_set,
                         NotificationSet::Create());
    ASSERT_OK(
        notification_set
            ->AddExternal(NativeWaitHandle{.type = NativeWaitHandleType::kWindowsHandle,
                                           .value = reinterpret_cast<intptr_t>(event)},
                          NotificationCallbacks([] { return absl::OkStatus(); }, []() noexcept {}))
            .status());
    ASSERT_OK(notification_set->Seal());
    ASSERT_OK(notification_set->Arm());
    ASSERT_OK_AND_ASSIGN(
        BlockingCallbackThread waiter,
        BlockingCallbackThread::Start([&notification_set] { return notification_set->Wait(); }));

    ASSERT_TRUE(SetEvent(event));
    ASSERT_OK(waiter.Wait());
    notification_set->Disarm();
  }

  EXPECT_TRUE(CloseHandle(event));
}
#endif

}  // namespace
}  // namespace zebes
