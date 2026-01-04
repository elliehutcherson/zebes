#pragma once

#include <string>

#include "absl/strings/str_cat.h"

struct Texture {
  std::string id;
  std::string name;
  std::string path;
  void* sdl_texture = nullptr;

  std::string name_id() const { return absl::StrCat(name, ",", id); }
};