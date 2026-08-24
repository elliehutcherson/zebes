#ifndef ZEBES_RESOURCES_RESOURCE_UTILS_H_
#define ZEBES_RESOURCES_RESOURCE_UTILS_H_

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "absl/status/status.h"

namespace zebes {

// Visits every JSON definition in `directory_path` and reports all loader
// failures together. Filesystem failures are translated into Status here so
// resource managers do not need exception-based error handling.
absl::Status LoadJsonDefinitions(
    const std::string& directory_path, const std::string& kind,
    const std::function<absl::Status(const std::filesystem::path&)>& load);

// Removes the old JSON file associated with a resource if its name has changed.
//
// args:
//   id: The unique identifier of the resource.
//   old_name: The previous name of the resource.
//   new_name: The new name of the resource.
//   directory_path: The absolute path to the directory containing the resource files.
void RemoveOldFileIfExists(const std::string& id, const std::string& old_name,
                           const std::string& new_name, const std::string& directory_path);

// Writes a complete file through a sibling temporary and publishes it with one
// rename. A failed write never leaves a truncated definition at `path`.
absl::Status WriteTextFileAtomically(const std::string& path, std::string_view contents);

}  // namespace zebes

#endif  // ZEBES_RESOURCES_RESOURCE_UTILS_H_
