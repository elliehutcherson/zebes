#pragma once

#include <map>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

inline constexpr int kNoTile = 0;

inline constexpr int kDefaultPlayerTextureX = 0;
inline constexpr int kDefaultPlayerTextureY = 0;
inline constexpr int kDefaultPlayerTextureW = 32;
inline constexpr int kDefaultPlayerTextureH = 44;
inline constexpr int kDefaultPlayerTextureSize = 4;
inline constexpr int kDefaultPlayerTicksPerSprite = 15;

inline constexpr float kDefaultPlayerHitboxW = 32.0f;
inline constexpr float kDefaultPlayerHitboxH = 32.0f;

inline constexpr int kDefaultPlayerRenderW = kDefaultPlayerTextureW * 2;
inline constexpr int kDefaultPlayerRenderH = kDefaultPlayerTextureH * 2;

inline constexpr float kDefaultGravity = 2.0;

// File Paths
static const std::string kZebesAssetsPath = "assets/zebes"; 
static const std::string kZebesDatabasePath = "assets/zebes/sql/zebes.db";
static const std::string kZebesTileMatrixPath = "layer-5.csv"; 
static const std::string kHudFont = "Courier-Prime.ttf"; 
static const std::string kSamusIdleLeftPath = "samus-idle-left.png"; 
static const std::string kSamusIdleRightPath = "samus-idle-right.png"; 
static const std::string kSamusTurningLeftPath = "samus-turning-left.png"; 
static const std::string kSamusTurningRightPath = "samus-turning-right.png"; 
static const std::string kSamusRunningLeftPath = "samus-running-left.png"; 
static const std::string kSamusRunningRightPath = "samus-running-right.png"; 
static const std::string kSamusJumpingRightPath = "samus-jumping-right.png"; 

static const std::string kBackgroundPath = "assets/sunny-land/PNG/environment/layers/back.png";
static const std::string kTileSetPath = "assets/sunny-land/PNG/environment/layers/tileset.png";
static const std::string kCustomTileSetPath = "assets/sunny-land/PNG/environment/layers/custom-tileset.png";

namespace zebes {

struct WindowConfig {
  std::string title; 
  int xpos = 0;
  int ypos = 0;
  int width = 0;
  int height = 0;
  int flags = 0;
};

struct BoundaryConfig {
  int x_min = 0;
  int x_max = 3000;
  int y_min = 0;
  int y_max = 1000;
};

struct TileConfig {
  int scale = 2;
  int source_width = 16;
  int source_height = 16;
  int size_x = 200;
  int size_y = 100;
  const int render_width = source_width * scale;
  const int render_height = source_height * scale;
};

struct PathConfig {
  // Execute path.
  std::string execute; 
  // Where all the assets live.
  std::string assets; 
  // The CSV containing all the tile values.
  std::string tile_matrix; 
  // The background.
  std::string background;
  // The path to the tileset.
  std::string tile_set;
  // The path to the custom tileset.
  std::string custom_tile_set;
  // Path to font file for hude.
  std::string hud_font;
  // Constructor, only needs the execute path.
  PathConfig(absl::string_view execute_path);
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
  int size() const {
    return sub_sprites.size();
  }
};

struct CollisionConfig {
  // Used to assign objects to an area.
  float area_width = 256;
  float area_height = 256;
};

class GameConfig {
 public:
  // Create the game config. 
  static GameConfig Create();
  // Constructor, probably should be private.
  GameConfig(WindowConfig window_config, PathConfig path_config);
  // Destructor, nothing special should happen so make it default. 
  ~GameConfig() = default;
  // Window config, mainly for SDL.
  WindowConfig window;
  // Paths for all the asets.
  PathConfig paths;
  // World boundaries.
  BoundaryConfig boundaries;
  // Config for the tiles.
  TileConfig tiles;
  // Config for collision manager.
  CollisionConfig collisions;

  int fps = 60;
  int frame_delay = 1000 / fps;
  bool full_screen = false;

  float gravity = kDefaultGravity;

  float player_starting_x = 10;
  float player_starting_y = 10;
  float player_hitbox_w = kDefaultPlayerHitboxW;
  float player_hitbox_h = kDefaultPlayerHitboxH;

  // Flags
  bool enable_player_hitbox_render = true;
  // bool enable_player_hitbox_render = false;
  bool enable_tile_hitbox_render = true;
  bool enable_hud_render = true;
};

}  // namespace zebes 