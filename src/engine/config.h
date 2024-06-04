#pragma once

#include <string>
#include <vector>

#include "SDL_video.h"

#include "absl/strings/string_view.h"
#include "absl/strings/str_format.h"

#include "nlohmann/json.hpp"

inline constexpr int kNoTile = 0;

inline constexpr float kDefaultGravity = 2.0;

// File Paths
static const std::string kZebesAssetsPath = "assets/zebes";
static const std::string kZebesDatabasePath = "assets/zebes/sql/zebes.db";
static const std::string kZebesTileMatrixPath = "layer-5.csv";
static const std::string kHudFont = "Courier-Prime.ttf";

static const std::string kBackgroundPath =
    "assets/sunny-land/PNG/environment/layers/back.png";
static const std::string kTileSetPath =
    "assets/sunny-land/PNG/environment/layers/tileset.png";
static const std::string kCustomTileSetPath =
    "assets/sunny-land/PNG/environment/layers/custom-tileset.png";

namespace zebes {

struct WindowConfig {
  std::string title = "Zebes";
  int xpos = SDL_WINDOWPOS_CENTERED;
  int ypos = SDL_WINDOWPOS_CENTERED;
  int width = 1400;
  int height = 640;
  int flags = 0;

  // Define to_json and from_json functions for serialization and
  // deserialization
  friend void to_json(nlohmann::json &j, const WindowConfig &s) {
    j = nlohmann::json{
        {"title", s.title},
        {"xpos", s.xpos, "ypos", s.ypos},
        {"width", s.width, "height", s.height, "flags", s.flags}};
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
  std::string relative_tile_matrix = kZebesTileMatrixPath;
  std::string relative_background = kBackgroundPath;
  std::string relative_tile_set = kTileSetPath;
  std::string relative_custom_tile_set = kCustomTileSetPath;
  std::string relative_hud_font = kHudFont;

  PathConfig(absl::string_view execute_path);
  std::string assets() const {
    return absl::StrFormat("%s/%s", execute_, relative_assets);
  }
  std::string tile_matrix() const {
    return absl::StrFormat("%s/%s", assets(), relative_tile_matrix);
  }
  std::string background() const {
    return absl::StrFormat("%s/%s", execute_, relative_background);
  }
  std::string tile_set() const {
    return absl::StrFormat("%s/%s", execute_, relative_tile_set);
  }
  std::string custom_tile_set() const {
    return absl::StrFormat("%s/%s", execute_, relative_custom_tile_set);
  }
  std::string hud_font() const {
    return absl::StrFormat("%s/%s", assets(), relative_hud_font);
  }

  // Define to_json and from_json functions for serialization and
  // deserialization.
  friend void to_json(nlohmann::json &j, const PathConfig &s) {
    j = nlohmann::json{
        {"relative_assets", s.relative_assets},
        {"relative_tile_matrix", s.relative_tile_matrix},
        {"relative_background", s.relative_background},
        {"relative_tile_set", s.relative_tile_set},
        {"relative_custom_tile_set", s.relative_custom_tile_set},
        {"relative_hud_font", s.relative_hud_font},
    };
  }

  friend void from_json(const nlohmann::json &j, PathConfig &s) {
    j.at("relative_assets").get_to(s.relative_assets);
    j.at("relative_tile_matrix").get_to(s.relative_tile_matrix);
    j.at("relative_background").get_to(s.relative_background);
    j.at("relative_tile_set").get_to(s.relative_tile_set);
    j.at("relative_custom_tile_set").get_to(s.relative_custom_tile_set);
    j.at("relative_hud_font").get_to(s.relative_hud_font);
  }

private:
  // Execute path.
  std::string execute_;
};

enum class SpriteType : int {
  Invalid = -1,
  kEmpty = 0,
  kGrass1 = 1,
  kGrass2 = 2,
  kGrass3 = 3,
  kDirt1 = 4,
  kGrassSlopeUpRight = 5,
  kGrass1Left = 6,
  kGrass1Right = 7,
  kGrass1Down = 8,
  kGrassCornerUpLeft = 9,
  kGrassCornerUpRight = 10,
  kGrassCornerDownLeft = 11,
  kGrassCornerDownRight = 12,
  SamusIdleLeft = 100,
  SamusIdleRight = 101,
  SamusTurningLeft = 102,
  SamusTurningRight = 103,
  SamusRunningLeft = 104,
  SamusRunningRight = 105,
  SamusJumpingRight = 106,
};

struct SubSprite {
  // Size of the sources texture sprites.
  int texture_x = 0;
  int texture_y = 0;
  int texture_w = 0;
  int texture_h = 0;
  // The offset from the left most point of the hitbox
  // at the scale of the source texture.
  int texture_offset_x = 0;
  // The offset from the upper most point of the hitbox
  // at the scale of the source texture.
  int texture_offset_y = 0;
  // The width that the texture will be rendered.
  int render_w = 0;
  // The height that the texture will be rendered.
  int render_h = 0;
  // The offset from the left most point of the hitbox
  // at the scale of the destination render.
  int render_offset_x = 0;
  // The offset from the upper most point of the hitbox
  // at the scale of the destination render.
  int render_offset_y = 0;
};

struct SpriteConfig {
  // The type of the sprite, running right, idle left, etc.
  SpriteType type = SpriteType::Invalid;
  // Path from the binary to the png file.
  std::string texture_path = "";
  // Ticks each sub sprite is rendered per sprite cycle.
  int ticks_per_sprite = 0;
  // Per texture and sprite config, there are multiple sub sprites
  // containing the config for each animation.
  std::vector<SubSprite> sub_sprites;
  // Return the size of sub sprites.
  int size() const { return sub_sprites.size(); }
};

class GameConfig {
public:
  enum Mode { kPlayerMode = 0, kCreatorMode = 1 };
  // Create the game config.
  static GameConfig Create();
  // Constructor, probably should be private.
  GameConfig(WindowConfig window_config, PathConfig path_config);
  // Destructor, nothing special should happen so make it default.
  ~GameConfig() = default;

  WindowConfig window;
  PathConfig paths;
  BoundaryConfig boundaries;
  TileConfig tiles;
  CollisionConfig collisions;

  Mode mode = Mode::kPlayerMode;
  int fps = 60;
  int frame_delay = 1000 / fps;
  bool full_screen = false;

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
        {"full_screen", s.full_screen},
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
    j.at("full_screen").get_to(s.full_screen);
    j.at("gravity").get_to(s.gravity);
    j.at("player_starting_x").get_to(s.player_starting_x);
    j.at("player_starting_y").get_to(s.player_starting_y);
    j.at("player_hitbox_w").get_to(s.player_hitbox_w);
    j.at("player_hitbox_h").get_to(s.player_hitbox_h);
    j.at("enable_player_hitbox_render").get_to(s.enable_player_hitbox_render);
    j.at("enable_tile_hitbox_render").get_to(s.enable_tile_hitbox_render);
    j.at("enable_hud_render").get_to(s.enable_hud_render);
  }
};

} // namespace zebes