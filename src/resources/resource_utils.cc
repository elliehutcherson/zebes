#include "resources/resource_utils.h"

#include <filesystem>

#include "absl/strings/str_cat.h"

namespace zebes {

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
