#include "config.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#if __APPLE__
#include <mach-o/dyld.h>
#elif __linux__
#include <filesystem>
#endif

namespace zebes {

PathConfig::PathConfig(absl::string_view execute_path)
    : execute_(execute_path) {};

std::string GameConfig::GetExecPath() {
  std::string path;
#if __APPLE__
  char buf[PATH_MAX];
  uint32_t bufsize = PATH_MAX;
  if (!_NSGetExecutablePath(buf, &bufsize)) {
    path = std::string(buf);
  }
#elif __linux__
  path = std::filesystem::canonical("/proc/self/exe");
#endif
  std::vector<std::string> paths = absl::StrSplit(path, "/");
  paths.pop_back();
  return absl::StrJoin(paths, "/");
}

GameConfig GameConfig::Create() {
  WindowConfig window_config;
  PathConfig path_config(GetExecPath());
  GameConfig config(window_config, path_config);
  return config;
}

GameConfig::GameConfig(WindowConfig window_config, PathConfig path_config)
    : window(window_config), paths(path_config) {}


} // namespace zebes