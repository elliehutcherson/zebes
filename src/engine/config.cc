#include "config.h"

#include <fstream>

#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#if __APPLE__
#include <mach-o/dyld.h>
#elif __linux__
#include <filesystem>
#endif

namespace zebes {

PathConfig::PathConfig(absl::string_view execute_path)
    : execute_(execute_path){};

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

std::string GameConfig::GetDefaultConfigPath() {
  return absl::StrCat(GetExecPath(), "/config.json");
}

absl::StatusOr<GameConfig> GameConfig::Create(std::optional<std::string> path) {
  if (path.has_value()) {
    return GameConfig::LoadConfig(*path);
  }
  WindowConfig window_config;
  PathConfig path_config(GetExecPath());
  GameConfig config;
  config.window = window_config;
  config.paths = path_config;
  return config;
}

GameConfig::GameConfig() : paths(PathConfig(GetExecPath())) {}

absl::StatusOr<GameConfig> GameConfig::LoadConfig(const std::string &path) {
  LOG(INFO) << __func__ << ": "
            << "Importing config from path: " << path;

  std::ifstream file(path);
  if (file.fail() || !file.is_open()) {
    return absl::NotFoundError(absl::StrCat("Failed to open file: ", path));
  }

  std::stringstream file_contents;
  file_contents << file.rdbuf();
  file.close();

  nlohmann::json j;
  j = nlohmann::json::parse(file_contents.str());

  GameConfig config;
  nlohmann::from_json(j, config);

  LOG(INFO) << __func__ << ": "
            << "Successfully imported: " << j.dump(2);

  return config;
}

}  // namespace zebes