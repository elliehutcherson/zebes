#pragma once

#include <string>

#include "engine/texture_handle.h"

#include "absl/strings/str_cat.h"

struct Texture {
  std::string id;
  std::string name;
  std::string path;
  zebes::TextureHandle texture_handle;

  std::string name_id() const { return absl::StrCat(name, "-", id); }
};

inline std::vector<Texture> DummyTextures() {
  return {
      {
          .id = "0",
          .name = "sky_background",
          .path = "assets/textures/parallax/sky.png",
          .texture_handle = {},
      },
      {
          .id = "1",
          .name = "distant_mountains",
          .path = "assets/textures/parallax/mountains_far.png",
          .texture_handle = {},
      },
      {
          .id = "2",
          .name = "near_trees",
          .path = "assets/textures/parallax/trees_near.png",
          .texture_handle = {},
      },
      {
          .id = "3",
          .name = "ground_tiles",
          .path = "assets/textures/tileset_01.png",
          .texture_handle = {},
      },
  };
}
