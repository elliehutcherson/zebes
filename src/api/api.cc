#include "api/api.h"

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Api>> Api::Create(const Options& options) {
  if (options.config == nullptr) {
    return absl::InvalidArgumentError("EngineConfig is null.");
  }
  if (options.texture_manager == nullptr) {
    return absl::InvalidArgumentError("TextureManager is null.");
  }
  if (options.sprite_manager == nullptr) {
    return absl::InvalidArgumentError("SpriteManager is null.");
  }
  if (options.collider_manager == nullptr) {
    return absl::InvalidArgumentError("ColliderManager is null.");
  }
  if (options.level_manager == nullptr) {
    return absl::InvalidArgumentError("LevelManager is null.");
  }
  if (options.tileset_manager == nullptr) {
    return absl::InvalidArgumentError("TilesetManager is null.");
  }
  // Checked like the rest. BlueprintManager was the one manager nobody
  // validated, so a null one reached the first call that used it instead of
  // failing here.
  if (options.blueprint_manager == nullptr) {
    return absl::InvalidArgumentError("BlueprintManager is null.");
  }
  if (options.terrain_recipe_manager == nullptr) {
    return absl::InvalidArgumentError("TerrainRecipeManager is null.");
  }
  return std::unique_ptr<Api>(new Api(options));
}

Api::Api(const Options& options)
    : config_(*options.config),
      texture_manager_(options.texture_manager),
      sprite_manager_(options.sprite_manager),
      collider_manager_(options.collider_manager),
      blueprint_manager_(options.blueprint_manager),
      level_manager_(options.level_manager),
      tileset_manager_(options.tileset_manager),
      terrain_recipe_manager_(options.terrain_recipe_manager) {}

absl::Status Api::SaveConfig(const EngineConfig& config) {
  LOG(INFO) << "SaveConfig in the api....";
  absl::Status status = EngineConfig::Save(config);
  if (!status.ok()) return status;

  // Publish the saved settings to long-lived editor consumers. The Api holds
  // the EditorEngine-owned config by reference, so this does not introduce a
  // second source of truth.
  config_ = config;
  return absl::OkStatus();
}

absl::StatusOr<std::string> Api::CreateTexture(Texture texture) {
  // Delegate to TextureManager
  return texture_manager_->CreateTexture(texture);
}

absl::StatusOr<std::string> Api::CreateTextureFromPixels(const std::string& name, int width,
                                                         int height,
                                                         absl::Span<const uint8_t> pixels) {
  return texture_manager_->CreateTextureFromPixels(name, width, height, pixels);
}

absl::Status Api::ReplaceTexturePixels(const std::string& texture_id, int width, int height,
                                       absl::Span<const uint8_t> pixels) {
  return texture_manager_->ReplaceTexturePixels(texture_id, width, height, pixels);
}

absl::Status Api::ShowTexturePixels(const std::string& texture_id, int width, int height,
                                    absl::Span<const uint8_t> pixels) {
  return texture_manager_->ShowTexturePixels(texture_id, width, height, pixels);
}

absl::Status Api::UpdateTexture(const Texture& texture) {
  return texture_manager_->UpdateTexture(texture);
}

absl::Status Api::DeleteTexture(const std::string& texture_id) {
  if (sprite_manager_->IsTextureUsed(texture_id)) {
    return absl::FailedPreconditionError("Texture is currently in use by a sprite.");
  }
  return texture_manager_->DeleteTexture(texture_id);
}

absl::StatusOr<std::vector<Texture>> Api::GetAllTextures() {
  return texture_manager_->GetAllTextures();
}

absl::StatusOr<TextureHandle> Api::GetTextureHandle(const std::string& texture_id) {
  return texture_manager_->GetTextureHandle(texture_id);
}

absl::StatusOr<Texture*> Api::GetTexture(const std::string& id) {
  return texture_manager_->GetTexture(id);
}

absl::StatusOr<std::string> Api::CreateSprite(Sprite sprite) {
  return sprite_manager_->CreateSprite(std::move(sprite));
}

absl::Status Api::UpdateSprite(Sprite sprite) { return sprite_manager_->SaveSprite(sprite); }

absl::Status Api::DeleteSprite(const std::string& sprite_id) {
  if (blueprint_manager_->IsSpriteUsed(sprite_id)) {
    return absl::FailedPreconditionError("Sprite is currently in use by a blueprint.");
  }
  return sprite_manager_->DeleteSprite(sprite_id);
}

std::vector<Sprite> Api::GetAllSprites() { return sprite_manager_->GetAllSprites(); }

absl::StatusOr<Sprite*> Api::GetSprite(const std::string& sprite_id) {
  return sprite_manager_->GetSprite(sprite_id);
}

absl::StatusOr<std::string> Api::CreateCollider(Collider collider) {
  return collider_manager_->CreateCollider(std::move(collider));
}

absl::Status Api::UpdateCollider(Collider collider) {
  return collider_manager_->SaveCollider(std::move(collider));
}

absl::Status Api::DeleteCollider(const std::string& collider_id) {
  if (blueprint_manager_->IsColliderUsed(collider_id)) {
    return absl::FailedPreconditionError("Collider is currently in use by a blueprint.");
  }
  return collider_manager_->DeleteCollider(collider_id);
}

std::vector<Collider> Api::GetAllColliders() { return collider_manager_->GetAllColliders(); }

absl::StatusOr<Collider*> Api::GetCollider(const std::string& collider_id) {
  return collider_manager_->GetCollider(collider_id);
}

absl::StatusOr<std::string> Api::CreateBlueprint(Blueprint blueprint) {
  return blueprint_manager_->CreateBlueprint(std::move(blueprint));
}

absl::Status Api::UpdateBlueprint(Blueprint blueprint) {
  return blueprint_manager_->SaveBlueprint(std::move(blueprint));
}

absl::Status Api::DeleteBlueprint(const std::string& blueprint_id) {
  return blueprint_manager_->DeleteBlueprint(blueprint_id);
}

std::vector<Blueprint> Api::GetAllBlueprints() { return blueprint_manager_->GetAllBlueprints(); }

absl::StatusOr<Blueprint*> Api::GetBlueprint(const std::string& blueprint_id) {
  return blueprint_manager_->GetBlueprint(blueprint_id);
}

absl::StatusOr<std::string> Api::CreateLevel(Level level) {
  return level_manager_->CreateLevel(std::move(level));
}

absl::Status Api::UpdateLevel(Level level) { return level_manager_->SaveLevel(std::move(level)); }

absl::Status Api::DeleteLevel(const std::string& level_id) {
  return level_manager_->DeleteLevel(level_id);
}

std::vector<Level> Api::GetAllLevels() { return level_manager_->GetAllLevels(); }

absl::StatusOr<Level*> Api::GetLevel(const std::string& level_id) {
  return level_manager_->GetLevel(level_id);
}

absl::StatusOr<std::string> Api::CreateTileset(Tileset tileset) {
  return tileset_manager_->CreateTileset(std::move(tileset));
}

absl::Status Api::UpdateTileset(Tileset tileset) { return tileset_manager_->SaveTileset(tileset); }

absl::Status Api::DeleteTileset(const std::string& tileset_id) {
  return tileset_manager_->DeleteTileset(tileset_id);
}

std::vector<Tileset> Api::GetAllTilesets() { return tileset_manager_->GetAllTilesets(); }

absl::StatusOr<Tileset*> Api::GetTileset(const std::string& tileset_id) {
  return tileset_manager_->GetTileset(tileset_id);
}

absl::StatusOr<std::string> Api::CreateTerrainRecipe(TerrainRecipe recipe) {
  return terrain_recipe_manager_->CreateRecipe(std::move(recipe));
}

absl::Status Api::SaveTerrainRecipe(const TerrainRecipe& recipe) {
  return terrain_recipe_manager_->SaveRecipe(recipe);
}

absl::Status Api::DeleteTerrainRecipe(const std::string& recipe_id) {
  return terrain_recipe_manager_->DeleteRecipe(recipe_id);
}

std::vector<TerrainRecipe> Api::GetAllTerrainRecipes() const {
  return terrain_recipe_manager_->GetAllRecipes();
}

absl::StatusOr<TerrainRecipe*> Api::GetTerrainRecipe(const std::string& recipe_id) {
  return terrain_recipe_manager_->GetRecipe(recipe_id);
}

absl::StatusOr<std::optional<TerrainRecipe>> Api::FindTerrainRecipeForTileset(
    const std::string& tileset_id) {
  if (tileset_id.empty()) return std::nullopt;

  std::optional<TerrainRecipe> found;
  for (TerrainRecipe& recipe : terrain_recipe_manager_->GetAllRecipes()) {
    if (recipe.tileset_id != tileset_id) continue;
    if (found.has_value()) {
      return absl::FailedPreconditionError(
          absl::StrCat("recipes '", found->id, "' and '", recipe.id,
                       "' both claim tileset ", tileset_id,
                       "; which one regenerates it would be arbitrary"));
    }
    found = std::move(recipe);
  }
  return found;
}

}  // namespace zebes
