#pragma once

#include <map>
#include <string>

#include "engine/texture_handle.h"
#include "objects/collider.h"
#include "objects/level.h"
#include "objects/parallax_theme.h"
#include "objects/sprite.h"
#include "objects/tileset.h"

namespace zebes {

// Immutable authored definitions needed to simulate and compose one level.
// Values are copied out of their managers so frame and simulation code never
// borrow mutable catalog entries.
struct LoadedLevelContent {
  Level level;
  Tileset tileset;
  std::map<std::string, Sprite> sprites;
  std::map<std::string, Collider> colliders;
  std::map<std::string, ParallaxTheme> parallax_themes;
};

// Runtime texture bindings for LoadedLevelContent. Handles remain valid only
// while the TextureResourceStore behind the supplying TextureManager is alive.
struct LevelRenderResources {
  TextureHandle tileset_atlas;
  std::map<std::string, TextureHandle> sprite_textures;
  std::map<std::string, TextureHandle> parallax_textures;
};

// One atomically resolved level graph. Keeping definitions and bindings beside
// each other makes their shared lifetime explicit without exposing render
// resources to simulation APIs.
struct LoadedLevelAssets {
  LoadedLevelContent content;
  LevelRenderResources rendering;
};

}  // namespace zebes
