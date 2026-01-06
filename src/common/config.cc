#include "config.h"

#include <fstream>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#if __APPLE__
#include <mach-o/dyld.h>
#elif __linux__
#include <filesystem>
#endif

ABSL_FLAG(std::string, config_path, "", "Path to the config file");

namespace zebes {

std::string GetExecPath() {
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

EngineConfig::EngineConfig() : paths(GetExecPath()) {}

absl::StatusOr<EngineConfig> EngineConfig::Load(const std::string& path) {
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

  EngineConfig config;
  nlohmann::from_json(j, config);

  LOG(INFO) << __func__ << ": "
            << "Successfully imported: " << j.dump(2);

  return config;
}

absl::Status EngineConfig::Save(const EngineConfig& config) {
  nlohmann::json j;
  to_json(j, config);

  std::ofstream file(config.paths.config());
  if (file.fail() || !file.is_open()) {
    std::string error_msg =
        absl::StrCat("Failed to open file for writing: ", config.paths.config());
    LOG(ERROR) << __func__ << ": " << error_msg;
    return absl::InternalError(error_msg);
  }

  file << j.dump(2);
  file.close();

  LOG(INFO) << __func__ << ": "
            << "Successfully saved config to: " << config.paths.config();
  return absl::OkStatus();
}

absl::StatusOr<EngineConfig> EngineConfig::Create() {
  const std::string exec_path = GetExecPath();
  std::string config_path = absl::StrFormat("%s/%s", exec_path, kZebesConfigPath);
  if (!absl::GetFlag(FLAGS_config_path).empty()) {
    config_path = absl::StrFormat("%s/%s", exec_path, absl::GetFlag(FLAGS_config_path));
  }

  absl::StatusOr<EngineConfig> config = Load(config_path);
  if (config.ok()) {
    LOG(INFO) << "Successfully loaded config from: " << config_path;
    return *config;
  }

  if (!absl::GetFlag(FLAGS_config_path).empty()) {
    LOG(ERROR) << "Failed to load config from: " << config_path;
    return config.status();
  }

  LOG(INFO) << "No config file found, loading default";
  EngineConfig fresh_config;
  if (!absl::GetFlag(FLAGS_config_path).empty()) {
    fresh_config.paths.relative_assets = absl::GetFlag(FLAGS_config_path);
  }

  absl::Status save_result = Save(fresh_config);
  if (!save_result.ok()) {
    LOG(ERROR) << "Failed to save config to: " << fresh_config.paths.config();
    return save_result;
  }

  LOG(INFO) << "Successfully saved config to: " << fresh_config.paths.config();
  return fresh_config;
}

}  // namespace zebes