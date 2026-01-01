#ifndef ZEBES_RESOURCES_RESOURCE_UTILS_H_
#define ZEBES_RESOURCES_RESOURCE_UTILS_H_

#include <string>

namespace zebes {

// Removes the old JSON file associated with a resource if its name has changed.
//
// args:
//   id: The unique identifier of the resource.
//   old_name: The previous name of the resource.
//   new_name: The new name of the resource.
//   directory_path: The absolute path to the directory containing the resource files.
void RemoveOldFileIfExists(const std::string& id, const std::string& old_name,
                           const std::string& new_name, const std::string& directory_path);

}  // namespace zebes

#endif  // ZEBES_RESOURCES_RESOURCE_UTILS_H_
