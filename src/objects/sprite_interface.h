#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "SDL_image.h"
#include "SDL_render.h"

namespace zebes {

inline constexpr uint16_t kInvalidSpriteId = 0;

enum class SpriteType : uint16_t {
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
  Invalid = 255,
};

inline std::string SpriteTypeToString(SpriteType type) {
  switch (type) {
    case SpriteType::kEmpty:
      return "Empty";
    case SpriteType::kGrass1:
      return "Grass1";
    case SpriteType::kGrass2:
      return "Grass2";
    case SpriteType::kGrass3:
      return "Grass3";
    case SpriteType::kDirt1:
      return "Dirt1";
    case SpriteType::kGrassSlopeUpRight:
      return "GrassSlopeUpRight";
    case SpriteType::kGrass1Left:
      return "Grass1Left";
    case SpriteType::kGrass1Right:
      return "Grass1Right";
    case SpriteType::kGrass1Down:
      return "Grass1Down";
    case SpriteType::kGrassCornerUpLeft:
      return "GrassCornerUpLeft";
    case SpriteType::kGrassCornerUpRight:
      return "GrassCornerUpRight";
    case SpriteType::kGrassCornerDownLeft:
      return "GrassCornerDownLeft";
    case SpriteType::kGrassCornerDownRight:
      return "GrassCornerDownRight";
    case SpriteType::SamusIdleLeft:
      return "SamusIdleLeft";
    case SpriteType::SamusIdleRight:
      return "SamusIdleRight";
    case SpriteType::SamusTurningLeft:
      return "SamusTurningLeft";
    case SpriteType::SamusTurningRight:
      return "SamusTurningRight";
    case SpriteType::SamusRunningLeft:
      return "SamusRunningLeft";
    case SpriteType::SamusRunningRight:
      return "SamusRunningRight";
    case SpriteType::SamusJumpingRight:
      return "SamusJumpingRight";
    case SpriteType::Invalid:
      return "Invalid";
    default:
      return "Unknown";
  }
};

struct SpriteFrame {
  // Database ID
  uint16_t id = 0;

  // Ordering index
  int index = 0;

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

  // The number of game ticks this frame should be displayed for.
  int frames_per_cycle = 1;
};

struct SpriteConfig {
  // Id assigned to the sprite by db.
  uint16_t id = 0;

  // The type of the sprite, running right, idle left, etc.
  uint16_t type = 0;

  // The type of the sprite, running right, idle left, etc.
  std::string type_name;

  // Path from the binary to the png file.
  std::string texture_path;

  // Per texture and sprite config, there are multiple sprite frames
  // containing the config for each animation.
  std::vector<SpriteFrame> sprite_frames;

  // Return the size of sprite frames.
  int size() const { return sprite_frames.size(); }
};

class SpriteInterface {
 public:
  virtual ~SpriteInterface() = default;
  // Get Source texture.
  virtual SDL_Texture* GetTexture() const = 0;

  // This will get a texture that is specific to the sprite frame,
  // not the entire texture.
  virtual SDL_Texture* GetTextureCopy(int index) const = 0;

  // Get Source texture rectangle.
  virtual const SDL_Rect* GetSource(int index) const = 0;

  // Get assigned id for the sprite by the db.
  virtual uint16_t GetId() const = 0;

  // Get type id provided by the user.
  virtual uint16_t GetType() const = 0;

  // Get config.
  virtual const SpriteConfig* GetConfig() const = 0;

  // Get SpriteFrame.
  virtual const SpriteFrame* GetSpriteFrame(int index) const = 0;
};

}  // namespace zebes