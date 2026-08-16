#include "common/background_task.h"

#include <memory>
#include <stdexcept>
#include <type_traits>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

static_assert(!std::is_default_constructible_v<BackgroundTask<int>>);

TEST(BackgroundTaskTest, ReturnsATypedResultAfterFinishing) {
  ASSERT_OK_AND_ASSIGN(BackgroundTask<int> task,
                       BackgroundTask<int>::Start([] { return absl::StatusOr<int>(42); }));

  ASSERT_OK(task.Wait());
  ASSERT_OK_AND_ASSIGN(const bool ready, task.IsReady());
  EXPECT_TRUE(ready);
  ASSERT_OK_AND_ASSIGN(const int result, task.TakeResult());
  EXPECT_EQ(result, 42);
}

TEST(BackgroundTaskTest, PreservesAWorkerStatus) {
  ASSERT_OK_AND_ASSIGN(BackgroundTask<int> task, BackgroundTask<int>::Start([] {
                         return absl::StatusOr<int>(absl::DataLossError("broken"));
                       }));

  ASSERT_OK(task.Wait());
  const absl::Status status = task.TakeResult().status();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(status.message(), "broken");
}

TEST(BackgroundTaskTest, TranslatesAnEscapedWorkerExceptionToStatus) {
  ASSERT_OK_AND_ASSIGN(BackgroundTask<int> task,
                       BackgroundTask<int>::Start([]() -> absl::StatusOr<int> {
                         throw std::runtime_error("broken promise");
                       }));

  ASSERT_OK(task.Wait());
  const absl::Status status = task.TakeResult().status();
  EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
  EXPECT_EQ(status.message(), "Background task failed outside its status contract: broken promise");
}

TEST(BackgroundTaskTest, CarriesMoveOnlyResults) {
  ASSERT_OK_AND_ASSIGN(BackgroundTask<std::unique_ptr<int>> task,
                       BackgroundTask<std::unique_ptr<int>>::Start([] {
                         return absl::StatusOr<std::unique_ptr<int>>(std::make_unique<int>(7));
                       }));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<int> result, task.TakeResult());
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(*result, 7);
}

TEST(BackgroundTaskTest, RejectsASecondReadAfterTheResultIsConsumed) {
  ASSERT_OK_AND_ASSIGN(BackgroundTask<int> task,
                       BackgroundTask<int>::Start([] { return absl::StatusOr<int>(1); }));
  ASSERT_OK(task.TakeResult());

  EXPECT_EQ(task.TakeResult().status().code(), absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
