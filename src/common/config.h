#pragma once

#include <string>

#include "SDL_video.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

inline constexpr int kNoTile = 0;

inline constexpr float kDefaultGravity = 2.0;

// File Paths
static const std::string kZebesAssetsPath = "assets";
static const std::string kZebesConfigPath = "assets/config.json";
static const std::string kZebesDatabasePath = "assets/sql/zebes.db";
static const std::string kZebesMigrationsPath = "assets/sql/migrations";

namespace zebes {

struct WindowConfig {
  std::string title = "Zebes";
  uint32_t xpos = SDL_WINDOWPOS_CENTERED;
  uint32_t ypos = SDL_WINDOWPOS_CENTERED;
  int width = 1400;
  int height = 640;
  uint32_t flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

  bool full_screen() const { return flags & SDL_WINDOW_FULLSCREEN_DESKTOP; }

  // Define to_json and from_json functions for serialization and
  // deserialization
  friend void to_json(nlohmann::json& j, const WindowConfig& s) {
    j = nlohmann::json{{"title", s.title}, {"xpos", s.xpos},     {"ypos", s.ypos},
                       {"width", s.width}, {"height", s.height}, {"flags", s.flags}};
  }

  friend void from_json(const nlohmann::json& j, WindowConfig& s) {
    j.at("title").get_to(s.title);
    j.at("xpos").get_to(s.xpos);
    j.at("ypos").get_to(s.ypos);
    j.at("width").get_to(s.width);
    j.at("height").get_to(s.height);
    j.at("flags").get_to(s.flags);
  }
};

struct PathConfig {
  std::string relative_assets = kZebesAssetsPath;

  explicit PathConfig(absl::string_view execute_path) : execute_(execute_path) {};
  std::string execute() const { return execute_; }
  std::string config() const { return absl::StrFormat("%s/%s", execute_, kZebesConfigPath); }
  std::string assets() const { return absl::StrFormat("%s/%s", execute_, relative_assets); }
  std::string database() const { return absl::StrFormat("%s/%s", execute_, kZebesDatabasePath); }
  std::string migrations() const {
    return absl::StrFormat("%s/%s", execute_, kZebesMigrationsPath);
  }
  std::string top() const { return absl::StrFormat("%s/%s", execute_, "../.."); }

  // Define to_json and from_json functions for serialization and
  // deserialization.
  friend void to_json(nlohmann::json& j, const PathConfig& s) {
    j = nlohmann::json{
        {"relative_assets", s.relative_assets},
    };
  }

  friend void from_json(const nlohmann::json& j, PathConfig& s) {
    j.at("relative_assets").get_to(s.relative_assets);
  }

 private:
  // Execute path.
  std::string execute_;
};

class EngineConfig {
 public:
  // Load the game config.
  static absl::StatusOr<EngineConfig> Load(const std::string& path);
  // Save the game config.
  static absl::Status Save(const EngineConfig& config);
  // Create the game config (load or default).
  static absl::StatusOr<EngineConfig> Create();

  explicit EngineConfig();
  ~EngineConfig() { LOG(INFO) << "EngineConfig destroyed"; }

  WindowConfig window;
  PathConfig paths;

  int fps = 60;
  int frame_delay = 1000 / fps;

  // Define to_json and from_json functions for serialization and
  // deserialization.
  friend void to_json(nlohmann::json& j, const EngineConfig& s) {
    j = nlohmann::json{
        {"window", s.window},
        {"paths", s.paths},
        {"fps", s.fps},
        {"frame_delay", s.frame_delay},
    };
  }

  friend void from_json(const nlohmann::json& j, EngineConfig& s) {
    j.at("window").get_to(s.window);
    j.at("paths").get_to(s.paths);

    j.at("fps").get_to(s.fps);
    j.at("frame_delay").get_to(s.frame_delay);
  }
};

}  // namespace zebes