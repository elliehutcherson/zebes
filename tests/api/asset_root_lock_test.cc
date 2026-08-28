#include "api/asset_root_lock.h"

#include <filesystem>
#include <memory>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

class AssetRootLockTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() / "zebes_asset_root_lock_test";
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  std::filesystem::path root_;
};

TEST_F(AssetRootLockTest, RejectsAConcurrentWriterWithOwnerDetails) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<AssetRootLock> first,
                       AssetRootLock::AcquireExclusive(root_, absl::ZeroDuration()));

  const absl::StatusOr<std::unique_ptr<AssetRootLock>> second =
      AssetRootLock::AcquireExclusive(root_, absl::ZeroDuration());

  ASSERT_EQ(second.status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_THAT(second.status().message(), ::testing::HasSubstr("pid="));
}

TEST_F(AssetRootLockTest, AWriterCanAcquireAfterTheOwnerReleases) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<AssetRootLock> first,
                       AssetRootLock::AcquireExclusive(root_, absl::ZeroDuration()));
  first.reset();

  EXPECT_TRUE(AssetRootLock::AcquireExclusive(root_, absl::ZeroDuration()).ok());
}

TEST_F(AssetRootLockTest, SharedReadersCoexistButBlockAWriter) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<AssetRootLock> first,
                       AssetRootLock::AcquireShared(root_, absl::ZeroDuration()));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<AssetRootLock> second,
                       AssetRootLock::AcquireShared(root_, absl::ZeroDuration()));

  EXPECT_EQ(AssetRootLock::AcquireExclusive(root_, absl::ZeroDuration()).status().code(),
            absl::StatusCode::kDeadlineExceeded);
}

TEST_F(AssetRootLockTest, WriterBlocksAReaderFromLoadingCatalogs) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<AssetRootLock> writer,
                       AssetRootLock::AcquireExclusive(root_, absl::ZeroDuration()));

  EXPECT_EQ(AssetRootLock::AcquireShared(root_, absl::ZeroDuration()).status().code(),
            absl::StatusCode::kDeadlineExceeded);
}

TEST_F(AssetRootLockTest, RejectsANonexistentAssetRoot) {
  std::filesystem::remove_all(root_);

  const absl::StatusOr<std::unique_ptr<AssetRootLock>> lock =
      AssetRootLock::AcquireExclusive(root_, absl::ZeroDuration());

  EXPECT_EQ(lock.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
