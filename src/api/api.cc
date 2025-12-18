#include "api/api.h"

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "common/status_macros.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Api>> Api::Create(const Options& options) {
  if (options.config == nullptr) {
    return absl::InvalidArgumentError("GameConfig is null.");
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
  return std::unique_ptr<Api>(new Api(options));
}

Api::Api(const Options& options)
    : config_(*options.config),
      texture_manager_(options.texture_manager),
      sprite_manager_(options.sprite_manager),
      collider_manager_(options.collider_manager) {}

absl::Status Api::SaveConfig(const GameConfig& config) {
  LOG(INFO) << "SaveConfig in the api....";
  return GameConfig::Save(config);
}

absl::StatusOr<std::string> Api::CreateTexture(Texture texture) {
  // Delegate to TextureManager
  return texture_manager_->CreateTexture(texture);
}

absl::Status Api::UpdateTexture(const Texture& texture) {
  return texture_manager_->UpdateTexture(texture);
}

absl::Status Api::DeleteTexture(const std::string& texture_id) {
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
  return collider_manager_->DeleteCollider(collider_id);
}

std::vector<Collider> Api::GetAllColliders() { return collider_manager_->GetAllColliders(); }

absl::StatusOr<Collider*> Api::GetCollider(const std::string& collider_id) {
  return collider_manager_->GetCollider(collider_id);
}

}  // namespace zebes