#pragma once

#include <filesystem>
#include <memory>

#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace zebes {

// Cross-process ownership guard for an authored asset root.
//
// Writers acquire this lock before loading any catalog and retain it until all
// managers have been destroyed. This prevents a second writer from committing
// a catalog snapshot that became stale while it was waiting to write.
class AssetRootLock {
 public:
  static absl::StatusOr<std::unique_ptr<AssetRootLock>> AcquireShared(
      const std::filesystem::path& asset_root, absl::Duration timeout);
  static absl::StatusOr<std::unique_ptr<AssetRootLock>> AcquireExclusive(
      const std::filesystem::path& asset_root, absl::Duration timeout);

  ~AssetRootLock();

  AssetRootLock(const AssetRootLock&) = delete;
  AssetRootLock& operator=(const AssetRootLock&) = delete;

  const std::filesystem::path& path() const { return path_; }

 private:
  enum class Mode { kShared, kExclusive };

  static absl::StatusOr<std::unique_ptr<AssetRootLock>> Acquire(
      const std::filesystem::path& asset_root, absl::Duration timeout, Mode mode);

  AssetRootLock(int descriptor, std::filesystem::path path);

  int descriptor_ = -1;
  std::filesystem::path path_;
};

}  // namespace zebes
