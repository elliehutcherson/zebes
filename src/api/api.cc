#include "api/api.h"

#include "absl/log/log.h"

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
  return std::unique_ptr<Api>(new Api(options));
}

Api::Api(const Options& options)
    : config_(*options.config),
      texture_manager_(options.texture_manager),
      sprite_manager_(options.sprite_manager),
      collider_manager_(options.collider_manager),
      blueprint_manager_(options.blueprint_manager),
      level_manager_(options.level_manager) {}

absl::Status Api::SaveConfig(const EngineConfig& config) {
  LOG(INFO) << "SaveConfig in the api....";
  return EngineConfig::Save(config);
}

absl::StatusOr<std::string> Api::CreateTexture(Texture texture) {
  // Delegate to TextureManager
  return texture_manager_->CreateTexture(texture);
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

}  // namespace zebes