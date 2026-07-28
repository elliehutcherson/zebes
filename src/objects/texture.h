#pragma once

#include <string>

#include "absl/strings/str_cat.h"

// A texture asset as authored: an identity and where its image lives on disk.
//
// Deliberately free of any runtime handle. The GPU resource for a texture is
// owned by the texture store and looked up by ID, which keeps this definition
// serializable and independent of the rendering backend.
struct Texture {
  std::string id;
  std::string name;
  std::string path;

  std::string name_id() const { return absl::StrCat(name, "-", id); }
};
