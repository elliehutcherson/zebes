#pragma once

#include <cstdint>
#include <string>

struct Texture {
  std::string id;
  std::string name;
  std::string path;
  void* sdl_texture = nullptr;
};