#include "api/asset_workspace.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "api/api.h"
#include "api/asset_root_lock.h"
#include "common/status_macros.h"
#include "resources/blueprint_manager.h"
#include "resources/collider_manager.h"
#include "resources/level_manager.h"
#include "resources/parallax_artwork_recipe_manager.h"
#include "resources/parallax_theme_manager.h"
#include "resources/prop_recipe_manager.h"
#include "resources/source_artwork_manager.h"
#include "resources/sprite_manager.h"
#include "resources/terrain_recipe_manager.h"
#include "resources/texture_manager.h"
#include "resources/texture_resource_store.h"
#include "resources/tileset_manager.h"

namespace zebes {
namespace {

absl::Status ValidateOptions(const AssetWorkspace::Options& options) {
  if (options.config == nullptr) {
    return absl::InvalidArgumentError("asset workspace needs an engine config");
  }
  if (options.texture_resources == nullptr) {
    return absl::InvalidArgumentError("asset workspace needs a texture resource store");
  }
  if (options.asset_root.empty()) {
    return absl::InvalidArgumentError("asset workspace root is empty");
  }
  switch (options.load_profile) {
    case AssetWorkspace::LoadProfile::kComplete:
      return absl::OkStatus();
    case AssetWorkspace::LoadProfile::kLevelReview:
      if (options.access != AssetWorkspace::Access::kReadOnly) {
        return absl::InvalidArgumentError("the level-review workspace profile is read-only");
      }
      return absl::OkStatus();
  }
  return absl::InvalidArgumentError("asset workspace load profile is invalid");
}

}  // namespace

AssetWorkspace::~AssetWorkspace() = default;

absl::StatusOr<AssetWorkspace::LoadProfile> AssetWorkspace::ParseLoadProfile(std::string_view id) {
  if (id == "complete") return LoadProfile::kComplete;
  if (id == "referenced-level") return LoadProfile::kLevelReview;
  return absl::InvalidArgumentError(
      "asset workspace load profile must be complete or referenced-level");
}

std::string_view AssetWorkspace::LoadProfileId(LoadProfile profile) {
  switch (profile) {
    case LoadProfile::kComplete:
      return "complete";
    case LoadProfile::kLevelReview:
      return "referenced-level";
  }
  return "invalid";
}

absl::StatusOr<std::unique_ptr<AssetWorkspace>> AssetWorkspace::Create(Options options) {
  RETURN_IF_ERROR(ValidateOptions(options));

  auto workspace = absl::WrapUnique(new AssetWorkspace());
  RETURN_IF_ERROR(workspace->Init(options));
  return workspace;
}

absl::Status AssetWorkspace::Init(const Options& options) {
  const bool complete = options.load_profile == LoadProfile::kComplete;
  if (options.access == Access::kReadWrite) {
    ASSIGN_OR_RETURN(catalog_lock_, AssetRootLock::AcquireExclusive(options.asset_root,
                                                                    options.write_lock_timeout));
  } else {
    ASSIGN_OR_RETURN(catalog_lock_,
                     AssetRootLock::AcquireShared(options.asset_root, options.write_lock_timeout));
  }

  ASSIGN_OR_RETURN(texture_manager_,
                   TextureManager::Create(options.texture_resources, options.asset_root));
  RETURN_IF_ERROR(texture_manager_->LoadAllTextures());

  ASSIGN_OR_RETURN(sprite_manager_,
                   SpriteManager::Create(texture_manager_.get(), options.asset_root));
  RETURN_IF_ERROR(sprite_manager_->LoadAllSprites());

  ASSIGN_OR_RETURN(collider_manager_, ColliderManager::Create(options.asset_root));
  if (complete) RETURN_IF_ERROR(collider_manager_->LoadAllColliders());

  ASSIGN_OR_RETURN(blueprint_manager_, BlueprintManager::Create(options.asset_root));
  if (complete) RETURN_IF_ERROR(blueprint_manager_->LoadAllBlueprints());

  ASSIGN_OR_RETURN(level_manager_, LevelManager::Create(options.asset_root));
  RETURN_IF_ERROR(level_manager_->LoadAllLevels());

  ASSIGN_OR_RETURN(parallax_theme_manager_, ParallaxThemeManager::Create(options.asset_root));
  RETURN_IF_ERROR(parallax_theme_manager_->LoadAllThemes());

  ASSIGN_OR_RETURN(tileset_manager_, TilesetManager::Create(options.asset_root));
  RETURN_IF_ERROR(tileset_manager_->LoadAllTilesets());

  ASSIGN_OR_RETURN(terrain_recipe_manager_, TerrainRecipeManager::Create(options.asset_root));
  if (complete) RETURN_IF_ERROR(terrain_recipe_manager_->LoadAllRecipes());

  ASSIGN_OR_RETURN(source_artwork_manager_, SourceArtworkManager::Create(options.asset_root));
  if (complete) RETURN_IF_ERROR(source_artwork_manager_->LoadAllArtwork());

  ASSIGN_OR_RETURN(prop_recipe_manager_, PropRecipeManager::Create(options.asset_root));
  if (complete) RETURN_IF_ERROR(prop_recipe_manager_->LoadAllRecipes());

  ASSIGN_OR_RETURN(parallax_artwork_recipe_manager_,
                   ParallaxArtworkRecipeManager::Create(options.asset_root));
  if (complete) RETURN_IF_ERROR(parallax_artwork_recipe_manager_->LoadAllRecipes());

  ASSIGN_OR_RETURN(api_,
                   Api::Create({
                       .config = options.config,
                       .texture_manager = texture_manager_.get(),
                       .sprite_manager = sprite_manager_.get(),
                       .collider_manager = collider_manager_.get(),
                       .blueprint_manager = blueprint_manager_.get(),
                       .level_manager = level_manager_.get(),
                       .parallax_theme_manager = parallax_theme_manager_.get(),
                       .tileset_manager = tileset_manager_.get(),
                       .terrain_recipe_manager = terrain_recipe_manager_.get(),
                       .source_artwork_manager = source_artwork_manager_.get(),
                       .prop_recipe_manager = prop_recipe_manager_.get(),
                       .parallax_artwork_recipe_manager = parallax_artwork_recipe_manager_.get(),
                   }));
  if (options.access == Access::kReadOnly) catalog_lock_.reset();
  return absl::OkStatus();
}

}  // namespace zebes
