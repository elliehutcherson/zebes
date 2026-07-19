#pragma once

#include <cstdint>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"
#include "objects/game_view.h"

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
  bool centered = true;
  int x = 0;
  int y = 0;
  int width = 1400;
  int height = 640;
  bool fullscreen = false;
  bool resizable = true;
  bool high_dpi = true;

  friend void to_json(nlohmann::json& j, const WindowConfig& s) {
    j = nlohmann::json{{"title", s.title},
                       {"centered", s.centered},
                       {"x", s.x},
                       {"y", s.y},
                       {"width", s.width},
                       {"height", s.height},
                       {"fullscreen", s.fullscreen},
                       {"resizable", s.resizable},
                       {"high_dpi", s.high_dpi}};
  }

  friend void from_json(const nlohmann::json& j, WindowConfig& s) {
    j.at("title").get_to(s.title);
    j.at("width").get_to(s.width);
    j.at("height").get_to(s.height);

    if (j.contains("centered")) {
      j.at("centered").get_to(s.centered);
      s.x = j.value("x", 0);
      s.y = j.value("y", 0);
      s.fullscreen = j.value("fullscreen", false);
      s.resizable = j.value("resizable", true);
      s.high_dpi = j.value("high_dpi", true);
      return;
    }

    // Migration for configs written before WindowConfig stopped persisting
    // SDL constants. These numeric values are read-only legacy file-format
    // details; current engine state remains platform-neutral.
    constexpr uint32_t kLegacyCenteredPosition = 0x2FFF0000u;
    constexpr uint32_t kLegacyFullscreenDesktop = 0x00001001u;
    constexpr uint32_t kLegacyResizable = 0x00000020u;
    constexpr uint32_t kLegacyHighDpi = 0x00002000u;
    const uint32_t legacy_x = j.value("xpos", kLegacyCenteredPosition);
    const uint32_t legacy_y = j.value("ypos", kLegacyCenteredPosition);
    const uint32_t legacy_flags = j.value("flags", 0u);
    s.centered = legacy_x == kLegacyCenteredPosition && legacy_y == kLegacyCenteredPosition;
    if (!s.centered) {
      s.x = static_cast<int>(legacy_x);
      s.y = static_cast<int>(legacy_y);
    }
    s.fullscreen = (legacy_flags & kLegacyFullscreenDesktop) != 0;
    s.resizable = (legacy_flags & kLegacyResizable) != 0;
    s.high_dpi = (legacy_flags & kLegacyHighDpi) != 0;
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

  // Validates project settings before they are used or persisted.
  absl::Status Validate() const;

  explicit EngineConfig();
  ~EngineConfig() { LOG(INFO) << "EngineConfig destroyed"; }

  WindowConfig window;
  GameViewSize game_view;
  PathConfig paths;

  int fps = 60;
  int frame_delay = 1000 / fps;

  // Define to_json and from_json functions for serialization and
  // deserialization.
  friend void to_json(nlohmann::json& j, const EngineConfig& s) {
    j = nlohmann::json{
        {"window", s.window},
        {"game_view", {{"width", s.game_view.width}, {"height", s.game_view.height}}},
        {"paths", s.paths},
        {"fps", s.fps},
        {"frame_delay", s.frame_delay},
    };
  }

  friend void from_json(const nlohmann::json& j, EngineConfig& s) {
    j.at("window").get_to(s.window);
    if (j.contains("game_view")) {
      const nlohmann::json& game_view = j.at("game_view");
      game_view.at("width").get_to(s.game_view.width);
      game_view.at("height").get_to(s.game_view.height);
    }
    j.at("paths").get_to(s.paths);

    j.at("fps").get_to(s.fps);
    j.at("frame_delay").get_to(s.frame_delay);
  }
};

}  // namespace zebes
