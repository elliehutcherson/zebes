#pragma once

#include <memory>
#include <string>

#include "absl/status/statusor.h"

namespace zebes {

class Api;
class BlueprintManager;
class ColliderManager;
class EngineConfig;
class LevelManager;
class ParallaxArtworkRecipeManager;
class ParallaxThemeManager;
class PropRecipeManager;
class SourceArtworkManager;
class SpriteManager;
class TerrainRecipeManager;
class TextureManager;
class TextureResourceStore;
class TilesetManager;

// Platform-neutral composition root for the complete authored asset catalog.
//
// The interactive editor supplies an SDL texture store; headless tools supply
// a store with no window or GPU. Everything above that adapter is identical,
// including fail-fast loading, cross-resource API validation, and persistence.
class AssetWorkspace {
 public:
  struct Options {
    EngineConfig* config = nullptr;
    TextureResourceStore* texture_resources = nullptr;
    std::string asset_root;
  };

  static absl::StatusOr<std::unique_ptr<AssetWorkspace>> Create(Options options);

  ~AssetWorkspace();

  AssetWorkspace(const AssetWorkspace&) = delete;
  AssetWorkspace& operator=(const AssetWorkspace&) = delete;

  Api& api() { return *api_; }
  const Api& api() const { return *api_; }

 private:
  AssetWorkspace() = default;

  absl::Status Init(const Options& options);

  // Reverse declaration order is destruction order. Api borrows every manager,
  // and managers that resolve textures borrow TextureManager.
  std::unique_ptr<TextureManager> texture_manager_;
  std::unique_ptr<SpriteManager> sprite_manager_;
  std::unique_ptr<ColliderManager> collider_manager_;
  std::unique_ptr<BlueprintManager> blueprint_manager_;
  std::unique_ptr<LevelManager> level_manager_;
  std::unique_ptr<ParallaxThemeManager> parallax_theme_manager_;
  std::unique_ptr<TilesetManager> tileset_manager_;
  std::unique_ptr<TerrainRecipeManager> terrain_recipe_manager_;
  std::unique_ptr<SourceArtworkManager> source_artwork_manager_;
  std::unique_ptr<PropRecipeManager> prop_recipe_manager_;
  std::unique_ptr<ParallaxArtworkRecipeManager> parallax_artwork_recipe_manager_;
  std::unique_ptr<Api> api_;
};

}  // namespace zebes
