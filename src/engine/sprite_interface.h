#pragma once

#include <string>
#include <vector>

#include "SDL_image.h"
#include "SDL_render.h"

namespace zebes {

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
  // Id assigned to the sprite by db.
  uint16_t id = 0;

  // The type of the sprite, running right, idle left, etc.
  uint16_t type = 0;

  // The type of the sprite, running right, idle left, etc.
  std::string type_name;

  // Path from the binary to the png file.
  std::string texture_path;

  // Ticks each sub sprite is rendered per sprite cycle.
  int ticks_per_sprite = 0;

  // Per texture and sprite config, there are multiple sub sprites
  // containing the config for each animation.
  std::vector<SubSprite> sub_sprites;

  // Return the size of sub sprites.
  int size() const { return sub_sprites.size(); }
};

class SpriteInterface {
 public:
  // Update ticks
  virtual void Update() = 0;

  // Get Source texture rectangle.
  virtual void Reset() = 0;

  // Get Source texture.
  virtual SDL_Texture *GetTexture() = 0;

  // Get Source texture rectangle.
  virtual SDL_Rect *GetSource() = 0;

  // Get Source texture rectangle.
  virtual const SDL_Rect *GetSource(int index) const = 0;

  // Get assigned id for the sprite by the db.
  virtual uint16_t GetId() const = 0;

  // Get type id provided by the user.
  virtual uint16_t GetType() const = 0;

  // Get config.
  virtual const SpriteConfig *GetConfig() const = 0;

  // Get SubSprite.
  virtual const SubSprite *GetSubSprite(int index) const = 0;

  // Return the subsprite using the internal index.
  virtual const SubSprite *CurrentSubSprite() const = 0;

  // Get the index for the current cycle.
  virtual int GetIndex() const = 0;

  // Get number of cycles, or the number of times a full
  // animation has been played since reset.
  virtual uint16_t GetCycle() const = 0;

  // Get the ticks for the current cycle.
  virtual int GetTicks() const = 0;
};

}  // namespace zebes