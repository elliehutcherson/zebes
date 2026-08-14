#ifndef ZEBES_RESOURCES_RESOURCE_UTILS_H_
#define ZEBES_RESOURCES_RESOURCE_UTILS_H_

#include <string>
#include <vector>

#include "absl/status/status.h"

namespace zebes {

// Collects the definitions a bulk load could not read.
//
// Every LoadAll* used to log a failed file at WARNING and return OK, so a
// definition the editor could not parse simply vanished from the catalog: the
// sprite was gone, nothing said why, and the only evidence was a terminal
// nobody was watching. That also made strict parsing pointless, since a stale
// definition would disappear rather than be reported.
//
// The scan still reads every file, so one bad definition does not hide the
// others, and the accumulated result names all of them at once.
class ResourceLoadFailures {
 public:
  void Add(const std::string& path, const absl::Status& status);

  bool empty() const { return failures_.empty(); }

  // OK when nothing failed, otherwise one error naming every failed file.
  // `kind` names what was being loaded, e.g. "sprite".
  absl::Status ToStatus(const std::string& kind) const;

 private:
  std::vector<std::string> failures_;
};

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
