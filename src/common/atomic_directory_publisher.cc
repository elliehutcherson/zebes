#include "common/atomic_directory_publisher.h"

#include <filesystem>
#include <string>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "common/utils.h"

namespace zebes {
namespace {

absl::StatusOr<std::filesystem::path> ParseDestination(const std::string& output_path) {
  if (output_path.empty()) return absl::InvalidArgumentError("publication output path is empty");
  const std::filesystem::path output(output_path);
  if (output.filename().empty()) {
    return absl::InvalidArgumentError("publication output path must name a directory");
  }
  return output;
}

}  // namespace

absl::Status ValidateNewDirectoryDestination(const std::string& output_path) {
  const absl::StatusOr<std::filesystem::path> parsed = ParseDestination(output_path);
  if (!parsed.ok()) return parsed.status();
  std::error_code error;
  if (std::filesystem::exists(*parsed, error)) {
    return absl::AlreadyExistsError(
        absl::StrCat("publication output already exists: ", parsed->string()));
  }
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not inspect publication output: ", error.message()));
  }
  return absl::OkStatus();
}

absl::Status PublishNewDirectoryAtomically(const std::string& output_path,
                                           AtomicDirectoryWriter writer) {
  if (!writer) return absl::InvalidArgumentError("publication writer is missing");
  RETURN_IF_ERROR(ValidateNewDirectoryDestination(output_path));
  ASSIGN_OR_RETURN(const std::filesystem::path output, ParseDestination(output_path));

  std::error_code error;
  const std::filesystem::path parent =
      output.parent_path().empty() ? std::filesystem::current_path() : output.parent_path();
  std::filesystem::create_directories(parent, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create publication output parent: ", error.message()));
  }
  const std::filesystem::path staging =
      parent / absl::StrCat(output.filename().string(), ".staging-", GenerateGuid());
  if (!std::filesystem::create_directory(staging, error) || error) {
    return absl::InternalError(
        absl::StrCat("could not create publication staging directory: ", error.message()));
  }
  absl::Cleanup remove_staging = [&staging] {
    std::error_code ignored;
    std::filesystem::remove_all(staging, ignored);
  };

  RETURN_IF_ERROR(std::move(writer)(staging));
  std::filesystem::rename(staging, output, error);
  if (error) {
    return absl::InternalError(absl::StrCat("could not publish directory: ", error.message()));
  }
  std::move(remove_staging).Cancel();
  return absl::OkStatus();
}

}  // namespace zebes
