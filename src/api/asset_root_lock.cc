#include "api/asset_root_lock.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace zebes {
namespace {

constexpr char kLockDirectory[] = ".zebes";
constexpr char kLockFilename[] = "asset-write.lock";

std::string SystemError(const char* action) {
  return absl::StrCat(action, ": ", std::strerror(errno));
}

std::string ReadOwner(const std::filesystem::path& lock_path) {
  std::ifstream stream(lock_path);
  std::string owner;
  std::getline(stream, owner);
  return owner.empty() ? "owner metadata unavailable" : owner;
}

absl::Status WriteOwner(int descriptor) {
  const std::string owner =
      absl::StrCat("pid=", getpid(), " acquired_at=", absl::FormatTime(absl::Now()));
  if (ftruncate(descriptor, 0) != 0) {
    return absl::InternalError(SystemError("could not clear asset write lock metadata"));
  }
  if (lseek(descriptor, 0, SEEK_SET) < 0) {
    return absl::InternalError(SystemError("could not seek asset write lock metadata"));
  }
  const ssize_t written = write(descriptor, owner.data(), owner.size());
  if (written < 0 || static_cast<size_t>(written) != owner.size()) {
    return absl::InternalError(SystemError("could not write asset write lock metadata"));
  }
  return absl::OkStatus();
}

}  // namespace

AssetRootLock::AssetRootLock(int descriptor, std::filesystem::path path)
    : descriptor_(descriptor), path_(std::move(path)) {}

AssetRootLock::~AssetRootLock() {
  if (descriptor_ < 0) return;
  flock(descriptor_, LOCK_UN);
  close(descriptor_);
}

absl::StatusOr<std::unique_ptr<AssetRootLock>> AssetRootLock::AcquireShared(
    const std::filesystem::path& asset_root, absl::Duration timeout) {
  return Acquire(asset_root, timeout, Mode::kShared);
}

absl::StatusOr<std::unique_ptr<AssetRootLock>> AssetRootLock::AcquireExclusive(
    const std::filesystem::path& asset_root, absl::Duration timeout) {
  return Acquire(asset_root, timeout, Mode::kExclusive);
}

absl::StatusOr<std::unique_ptr<AssetRootLock>> AssetRootLock::Acquire(
    const std::filesystem::path& asset_root, absl::Duration timeout, Mode mode) {
  if (asset_root.empty()) return absl::InvalidArgumentError("asset root is empty");
  if (timeout < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("asset write lock timeout cannot be negative");
  }

  std::error_code error;
  if (!std::filesystem::is_directory(asset_root, error)) {
    if (error) {
      return absl::InvalidArgumentError(absl::StrCat("could not inspect asset root ",
                                                     asset_root.string(), ": ", error.message()));
    }
    return absl::InvalidArgumentError(
        absl::StrCat("asset root is not a directory: ", asset_root.string()));
  }

  const std::filesystem::path lock_directory = asset_root / kLockDirectory;
  std::filesystem::create_directories(lock_directory, error);
  if (error) {
    return absl::InternalError(absl::StrCat("could not create asset lock directory ",
                                            lock_directory.string(), ": ", error.message()));
  }
  const std::filesystem::path lock_path = lock_directory / kLockFilename;
  const int descriptor = open(lock_path.c_str(), O_CREAT | O_RDWR, 0666);
  if (descriptor < 0) {
    return absl::InternalError(SystemError("could not open asset write lock"));
  }
  absl::Cleanup close_descriptor = [descriptor] { close(descriptor); };

  const absl::Time deadline = absl::Now() + timeout;
  const int operation = mode == Mode::kExclusive ? LOCK_EX : LOCK_SH;
  while (flock(descriptor, operation | LOCK_NB) != 0) {
    if (errno == EINTR) continue;
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      return absl::InternalError(SystemError("could not acquire asset write lock"));
    }
    if (absl::Now() >= deadline) {
      return absl::DeadlineExceededError(
          absl::StrCat("asset root is being edited by another process (", ReadOwner(lock_path),
                       "): ", asset_root.string()));
    }
    absl::SleepFor(absl::Milliseconds(25));
  }

  absl::Cleanup unlock = [descriptor] { flock(descriptor, LOCK_UN); };
  if (mode == Mode::kExclusive) {
    const absl::Status owner_status = WriteOwner(descriptor);
    if (!owner_status.ok()) return owner_status;
  }
  std::move(unlock).Cancel();
  std::move(close_descriptor).Cancel();
  return std::unique_ptr<AssetRootLock>(new AssetRootLock(descriptor, lock_path));
}

}  // namespace zebes
