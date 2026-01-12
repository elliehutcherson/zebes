#pragma once

#include <string>

#include "absl/strings/str_cat.h"

struct Texture {
  std::string id;
  std::string name;
  std::string path;
  void* sdl_texture = nullptr;

  std::string name_id() const { return absl::StrCat(name, "-", id); }
};

inline std::vector<Texture> DummyTextures() {
  return {
      {
          .id = "0",
          .name = "sky_background",
          .path = "assets/textures/parallax/sky.png",
          .sdl_texture = nullptr,
      },
      {
          .id = "1",
          .name = "distant_mountains",
          .path = "assets/textures/parallax/mountains_far.png",
          .sdl_texture = nullptr,
      },
      {
          .id = "2",
          .name = "near_trees",
          .path = "assets/textures/parallax/trees_near.png",
          .sdl_texture = nullptr,
      },
      {
          .id = "3",
          .name = "ground_tiles",
          .path = "assets/textures/tileset_01.png",
          .sdl_texture = nullptr,
      },
  };
}