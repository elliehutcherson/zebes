#pragma once

#include <string_view>

#include "absl/status/statusor.h"
#include "resources/blueprint_manager.h"
#include "resources/collider_manager.h"
#include "resources/level_manager.h"
#include "resources/loaded_level_assets.h"
#include "resources/parallax_theme_manager.h"
#include "resources/sprite_manager.h"
#include "resources/texture_manager.h"
#include "resources/tileset_manager.h"

namespace zebes {

struct LevelAssetLoaderOptions {
  LevelManager& levels;
  TilesetManager& tilesets;
  SpriteManager& sprites;
  ColliderManager& colliders;
  BlueprintManager& blueprints;
  ParallaxThemeManager& parallax_themes;
  TextureManager& textures;
};

// Resolves and validates the complete transitive graph for one already-loaded
// level definition. Individual managers remain authoritative for catalog and
// texture loading; this helper only coordinates their results.
absl::StatusOr<LoadedLevelAssets> ResolveLevelAssets(const LevelAssetLoaderOptions& resources,
                                                     std::string_view level_id);

}  // namespace zebes
