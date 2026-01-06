#pragma once

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
};

struct Sprite {
  // Guid
  std::string id;
  // Name of the sprite
  std::string name;
  // Guid of the texture
  std::string texture_id;
  // Sprite frames
  std::vector<SpriteFrame> frames;
  // Pointer to the sdl texture
  void* sdl_texture = nullptr;

  std::string name_id() const { return absl::StrCat(name, "-", id); }
};

}  // namespace zebes