#include "creator_api.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

#include "nlohmann/detail/conversions/from_json.hpp"
#include "nlohmann/json.hpp"

#include "engine/config.h"

namespace zebes {

absl::StatusOr<CreatorApi>
CreatorApi::Create(const CreatorApi::Options &options) {
  LOG(INFO) << __func__ << ": " << "Initializing CreatorApi...";
  if (options.import_path.empty()) {
    return absl::InvalidArgumentError("Import path must not be empty.");
  }
  if (!std::filesystem::exists(options.import_path)) {
    return absl::InvalidArgumentError("Import path does not exist.");
  }
  return CreatorApi(options);
}

CreatorApi::CreatorApi(const Options &options)
    : import_path_(options.import_path) {}

absl::Status CreatorApi::SaveConfig(const GameConfig &config) {
  nlohmann::json j;
  nlohmann::to_json(j, config);

  LOG(INFO) << __func__ << ": " << "Saving config: " << j.dump(2);

  std::ofstream file(config.paths.config());
  if (!file.is_open()) {
    return absl::InternalError(
        absl::StrCat("Failed to open file: ", config.paths.config()));
  }
  file << j.dump(2);

  LOG(INFO) << __func__ << ": " << "Config saved to: " << config.paths.config();

  return absl::OkStatus();
}

absl::StatusOr<GameConfig> CreatorApi::ImportConfig() {
  std::string config_path = absl::StrCat(import_path_, "/", kZebesConfigPath);
  LOG(INFO) << __func__ << ": " << "Importing config from path: " << config_path;

  std::ofstream file(config_path);
  if (!file.is_open()) {
    return absl::InternalError(
        absl::StrCat("Failed to open file: ", import_path_));
  }

  std::stringstream file_contents;
  file_contents << file.rdbuf();

  nlohmann::json j;
  j = nlohmann::json::parse(file_contents.str());

  GameConfig config = GameConfig::Create();
  nlohmann::from_json(j, config);
  LOG(INFO) << __func__ << ": " << "Successfully imported: " << j.dump(2);

  return config;
}

} // namespace zebes