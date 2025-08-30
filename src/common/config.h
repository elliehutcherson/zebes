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
static const std::string kZebesConfigPath = "config.json";
static const std::string kZebesAssetsPath = "assets/zebes";
static const std::string kZebesDatabasePath = "assets/zebes/sql/zebes.db";
static const std::string kHudFont = "Courier-Prime.ttf";

namespace zebes {

struct WindowConfig {
  std::string title = "Zebes";
  uint32_t xpos = SDL_WINDOWPOS_CENTERED;
  uint32_t ypos = SDL_WINDOWPOS_CENTERED;
  int width = 1400;
  int height = 640;
  uint32_t flags = SDL_WINDOW_FULLSCREEN_DESKTOP;

  bool full_screen() const {
    return flags & SDL_WINDOW_FULLSCREEN_DESKTOP;
  }

  // Define to_json and from_json functions for serialization and
  // deserialization
  friend void to_json(nlohmann::json &j, const WindowConfig &s) {
    j = nlohmann::json{{"title", s.title},   {"xpos", s.xpos},
                       {"ypos", s.ypos},     {"width", s.width},
                       {"height", s.height}, {"flags", s.flags}};
  }

  friend void from_json(const nlohmann::json &j, WindowConfig &s) {
    j.at("title").get_to(s.title);
    j.at("xpos").get_to(s.xpos);
    j.at("ypos").get_to(s.ypos);
    j.at("width").get_to(s.width);
    j.at("height").get_to(s.height);
    j.at("flags").get_to(s.flags);
  }
};

struct BoundaryConfig {
  int x_min = 0;
  int x_max = 3000;
  int y_min = 0;
  int y_max = 1000;

  // Define to_json and from_json functions for serialization and
  // deserialization
  friend void to_json(nlohmann::json &j, const BoundaryConfig &s) {
    j = nlohmann::json{{"x_min", s.x_min},
                       {"x_max", s.x_max},
                       {"y_min", s.y_min},
                       {"y_max", s.y_max}};
  }

  friend void from_json(const nlohmann::json &j, BoundaryConfig &s) {
    j.at("x_min").get_to(s.x_min);
    j.at("x_max").get_to(s.x_max);
    j.at("y_min").get_to(s.y_min);
    j.at("y_max").get_to(s.y_max);
  }
};

struct TileConfig {
  int scale = 2;
  int source_width = 16;
  int source_height = 16;
  int size_x = 200;
  int size_y = 100;
  int render_width() const { return source_width * scale; }
  int render_height() const { return source_height * scale; }

  // Define to_json and from_json functions for serialization and
  // deserialization
  friend void to_json(nlohmann::json &j, const TileConfig &s) {
    j = nlohmann::json{
        {"scale", s.scale},
        {"source_width", s.source_width},
        {"source_height", s.source_height},
        {"size_x", s.size_x},
        {"size_y", s.size_y},
    };
  }

  friend void from_json(const nlohmann::json &j, TileConfig &s) {
    j.at("scale").get_to(s.scale);
    j.at("source_width").get_to(s.source_width);
    j.at("source_height").get_to(s.source_height);
    j.at("size_x").get_to(s.size_x);
    j.at("size_y").get_to(s.size_y);
  }
};

struct CollisionConfig {
  // Used to assign objects to an area.
  float area_width = 256;
  float area_height = 256;

  // Define to_json and from_json functions for serialization and
  // deserialization.
  friend void to_json(nlohmann::json &j, const CollisionConfig &s) {
    j = nlohmann::json{{"area_width", s.area_width},
                       {"area_height", s.area_height}};
  }

  friend void from_json(const nlohmann::json &j, CollisionConfig &s) {
    j.at("area_width").get_to(s.area_width);
    j.at("area_height").get_to(s.area_height);
  }
};

struct PathConfig {
  std::string relative_assets = kZebesAssetsPath;
  std::string relative_hud_font = kHudFont;

  PathConfig(absl::string_view execute_path);
  std::string execute() const { return execute_; }
  std::string config() const {
    return absl::StrFormat("%s/%s", execute_, kZebesConfigPath);
  }
  std::string assets() const {
    return absl::StrFormat("%s/%s", execute_, relative_assets);
  }
  std::string hud_font() const {
    return absl::StrFormat("%s/%s", assets(), relative_hud_font);
  }
  std::string top() const {
    return absl::StrFormat("%s/%s", execute_, "../..");
  }

  // Define to_json and from_json functions for serialization and
  // deserialization.
  friend void to_json(nlohmann::json &j, const PathConfig &s) {
    j = nlohmann::json{
        {"relative_assets", s.relative_assets},
        {"relative_hud_font", s.relative_hud_font},
    };
  }

  friend void from_json(const nlohmann::json &j, PathConfig &s) {
    j.at("relative_assets").get_to(s.relative_assets);
    j.at("relative_hud_font").get_to(s.relative_hud_font);
  }

 private:
  // Execute path.
  std::string execute_;
};

class GameConfig {
 public:
  enum Mode { kPlayerMode = 0, kCreatorMode = 1 };
  // Get the path of the executable.
  static std::string GetExecPath();
  // Get the path of the executable.
  static std::string GetDefaultConfigPath();
  // Create the game config.
  static absl::StatusOr<GameConfig> Create(
      std::optional<std::string> path = std::nullopt);
  // Destructor, nothing special should happen so make it default.
  ~GameConfig() {
    LOG(INFO) << "GameConfig destroyed";
  }

  WindowConfig window;
  PathConfig paths;
  BoundaryConfig boundaries;
  TileConfig tiles;
  CollisionConfig collisions;

  Mode mode = Mode::kPlayerMode;
  int fps = 60;
  int frame_delay = 1000 / fps;

  float gravity = kDefaultGravity;

  float player_starting_x = 10;
  float player_starting_y = 10;
  float player_hitbox_w = 32;
  float player_hitbox_h = 32;

  // Flags
  bool enable_player_hitbox_render = true;
  bool enable_tile_hitbox_render = true;
  bool enable_hud_render = true;

  // Define to_json and from_json functions for serialization and
  // deserialization.
  friend void to_json(nlohmann::json &j, const GameConfig &s) {
    j = nlohmann::json{
        {"window", s.window},
        {"paths", s.paths},
        {"boundaries", s.boundaries},
        {"tiles", s.tiles},
        {"collisions", s.collisions},
        {"mode", s.mode},
        {"fps", s.fps},
        {"frame_delay", s.frame_delay},
        {"gravity", s.gravity},
        {"player_starting_x", s.player_starting_x},
        {"player_starting_y", s.player_starting_y},
        {"player_hitbox_w", s.player_hitbox_w},
        {"player_hitbox_h", s.player_hitbox_h},
        {"enable_player_hitbox_render", s.enable_player_hitbox_render},
        {"enable_tile_hitbox_render", s.enable_tile_hitbox_render},
        {"enable_hud_render", s.enable_hud_render},
    };
  }

  friend void from_json(const nlohmann::json &j, GameConfig &s) {
    j.at("window").get_to(s.window);
    j.at("paths").get_to(s.paths);
    j.at("boundaries").get_to(s.boundaries);
    j.at("tiles").get_to(s.tiles);
    j.at("collisions").get_to(s.collisions);
    j.at("mode").get_to(s.mode);
    j.at("fps").get_to(s.fps);
    j.at("frame_delay").get_to(s.frame_delay);
    j.at("gravity").get_to(s.gravity);
    j.at("player_starting_x").get_to(s.player_starting_x);
    j.at("player_starting_y").get_to(s.player_starting_y);
    j.at("player_hitbox_w").get_to(s.player_hitbox_w);
    j.at("player_hitbox_h").get_to(s.player_hitbox_h);
    j.at("enable_player_hitbox_render").get_to(s.enable_player_hitbox_render);
    j.at("enable_tile_hitbox_render").get_to(s.enable_tile_hitbox_render);
    j.at("enable_hud_render").get_to(s.enable_hud_render);
  }

 private:
  static absl::StatusOr<GameConfig> LoadConfig(const std::string &path);
  // Constructor, probably should be private.
  GameConfig();
};

void SaveConfig(const GameConfig& config);

}  // namespace zebes