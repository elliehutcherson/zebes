#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"

namespace zebes {

struct SpriteFrame {
  int index = 0;
  int texture_x = 0;
  int texture_y = 0;
  int texture_w = 0;
  int texture_h = 0;
  int render_w = 0;
  int render_h = 0;
  int frames_per_cycle = 0;
  int offset_x = 0;
  int offset_y = 0;

  bool operator==(const SpriteFrame& other) const = default;
};

// A frame's rendered rectangle relative to the sprite origin, in logical
// pixels. The entity transform places that origin in world space; offset_x and
// offset_y place the frame's top-left corner relative to it. Texture atlas
// coordinates do not participate in this geometry.
//
// The widened coordinates make adding an authored offset and render dimension
// well-defined even at the limits of their serialized int representation.
struct SpriteFrameRenderBounds {
  int64_t left = 0;
  int64_t top = 0;
  int64_t right = 0;
  int64_t bottom = 0;

  constexpr bool IsValid() const { return right > left && bottom > top; }
};

constexpr SpriteFrameRenderBounds CalculateSpriteFrameRenderBounds(const SpriteFrame& frame) {
  const int64_t left = frame.offset_x;
  const int64_t top = frame.offset_y;
  return {
      .left = left,
      .top = top,
      .right = left + frame.render_w,
      .bottom = top + frame.render_h,
  };
}

struct Sprite {
  // Guid
  std::string id;
  // Name of the sprite
  std::string name;
  // Guid of the texture
  std::string texture_id;
  // Sprite frames
  std::vector<SpriteFrame> frames;

  bool operator==(const Sprite& other) const = default;

  std::string name_id() const { return absl::StrCat(name, "-", id); }
};

}  // namespace zebes
