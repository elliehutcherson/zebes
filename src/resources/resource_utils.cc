#include "resources/resource_utils.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace zebes {
namespace {

absl::Status DirectoryIterationError(const std::string& directory_path,
                                     const std::error_code& error) {
  return absl::InternalError(
      absl::StrCat("could not read definition directory ", directory_path, ": ", error.message()));
}

}  // namespace

absl::Status LoadJsonDefinitions(
    const std::string& directory_path, const std::string& kind,
    const std::function<absl::Status(const std::filesystem::path&)>& load) {
  std::error_code error;
  const bool exists = std::filesystem::exists(directory_path, error);
  if (error) return DirectoryIterationError(directory_path, error);
  if (!exists) {
    return absl::NotFoundError(
        absl::StrCat(kind, " definition directory not found: ", directory_path));
  }
  const bool is_directory = std::filesystem::is_directory(directory_path, error);
  if (error) return DirectoryIterationError(directory_path, error);
  if (!is_directory) {
    return absl::FailedPreconditionError(
        absl::StrCat(kind, " definition path is not a directory: ", directory_path));
  }

  std::filesystem::directory_iterator entry(directory_path, error);
  if (error) return DirectoryIterationError(directory_path, error);

  std::vector<std::string> failures;
  const std::filesystem::directory_iterator end;
  while (entry != end) {
    const std::filesystem::path path = entry->path();
    entry.increment(error);
    if (error) return DirectoryIterationError(directory_path, error);
    if (path.extension() != ".json") continue;

    const absl::Status status = load(path);
    if (!status.ok()) {
      failures.push_back(absl::StrCat(path.filename().string(), " (", status.message(), ")"));
    }
  }
  if (failures.empty()) return absl::OkStatus();
  return absl::DataLossError(absl::StrCat("could not load ", failures.size(), " ", kind,
                                          " definition(s): ", absl::StrJoin(failures, "; ")));
}

void RemoveOldFileIfExists(const std::string& id, const std::string& old_name,
                           const std::string& new_name, const std::string& directory_path) {
  if (old_name == new_name) {
    return;
  }

  std::string old_filename = absl::StrCat(old_name, "-", id, ".json");
  std::filesystem::path old_path = std::filesystem::path(directory_path) / old_filename;

  if (std::filesystem::exists(old_path)) {
    std::filesystem::remove(old_path);
  }
}

absl::Status WriteTextFileAtomically(const std::string& path, std::string_view contents) {
  const std::string temporary = absl::StrCat(path, ".tmp");
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create definition directory: ", error.message()));
  }
  {
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream.is_open()) {
      return absl::InternalError(absl::StrCat("could not write temporary file: ", temporary));
    }
    stream << contents;
    stream.flush();
    if (!stream.good()) {
      stream.close();
      std::filesystem::remove(temporary, error);
      return absl::InternalError(absl::StrCat("failed while writing temporary file: ", temporary));
    }
  }
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary);
    return absl::InternalError(absl::StrCat("could not publish file: ", error.message()));
  }
  return absl::OkStatus();
}

}  // namespace zebes
