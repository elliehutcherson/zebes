#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace zebes {

class Api;
class AssetRootLock;
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

// Platform-neutral composition root for authored asset catalogs.
//
// The interactive editor supplies an SDL texture store; headless tools supply
// a store with no window or GPU. Complete loading remains the default. The
// explicit read-only profiles leave unrelated authoring catalogs empty so
// renderers do not validate retained source pixels they cannot consume.
class AssetWorkspace {
 public:
  enum class Access {
    kReadOnly,
    kReadWrite,
  };

  enum class LoadProfile {
    kComplete,
    kLevelReview,
    kRuntime,
  };

  static absl::StatusOr<LoadProfile> ParseLoadProfile(std::string_view id);
  static std::string_view LoadProfileId(LoadProfile profile);

  struct Options {
    EngineConfig* config = nullptr;
    TextureResourceStore* texture_resources = nullptr;
    std::string asset_root;
    Access access = Access::kReadOnly;
    LoadProfile load_profile = LoadProfile::kComplete;
    absl::Duration write_lock_timeout = absl::Seconds(30);
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

  // Reverse declaration order is destruction order. The lock is acquired
  // before catalogs load and released after every manager is gone.
  std::unique_ptr<AssetRootLock> catalog_lock_;
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
