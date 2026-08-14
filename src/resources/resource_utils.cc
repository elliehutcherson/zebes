#include "resources/resource_utils.h"

#include <filesystem>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace zebes {

void ResourceLoadFailures::Add(const std::string& path, const absl::Status& status) {
  failures_.push_back(absl::StrCat(path, " (", status.message(), ")"));
}

absl::Status ResourceLoadFailures::ToStatus(const std::string& kind) const {
  if (failures_.empty()) return absl::OkStatus();
  return absl::DataLossError(absl::StrCat("could not load ", failures_.size(), " ", kind,
                                          " definition(s): ", absl::StrJoin(failures_, "; ")));
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

}  // namespace zebes
