#pragma once

#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "engine/texture_handle.h"
#include "objects/blueprint.h"
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
  // Blueprint bindings retain definition pointers after boot. Heap ownership
  // keeps those pointers stable if the hash table moves its entries.
  absl::flat_hash_map<std::string, std::unique_ptr<Blueprint>> blueprints;
  absl::flat_hash_map<std::string, Sprite> sprites;
  absl::flat_hash_map<std::string, Collider> colliders;
  absl::flat_hash_map<std::string, ParallaxTheme> parallax_themes;
};

// Runtime texture bindings for LoadedLevelContent. Handles remain valid only
// while the TextureResourceStore behind the supplying TextureManager is alive.
struct LevelRenderResources {
  TextureHandle tileset_atlas;
  absl::flat_hash_map<std::string, TextureHandle> sprite_textures;
  absl::flat_hash_map<std::string, TextureHandle> parallax_textures;
};

// One atomically resolved level graph. Keeping definitions and bindings beside
// each other makes their shared lifetime explicit without exposing render
// resources to simulation APIs.
struct LoadedLevelAssets {
  LoadedLevelContent content;
  LevelRenderResources rendering;
};

}  // namespace zebes
