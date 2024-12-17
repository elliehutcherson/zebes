#include "sprite_object.h"

#include <cstdint>
#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "camera.h"
#include "object.h"
#include "object_interface.h"
#include "polygon.h"
#include "sprite_interface.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<SpriteObject>> SpriteObject::Create(
    ObjectOptions &options) {
  if (options.vertices.empty())
    return absl::InvalidArgumentError("Vertices must not be empty.");

  return std::unique_ptr<SpriteObject>(new SpriteObject(options));
}

SpriteObject::SpriteObject(ObjectOptions &options) : Object(options) { return; }

absl::Status SpriteObject::AddSprite(const SpriteInterface *sprite) {
  const uint16_t sprite_id = sprite->GetId();
  if (sprites_.contains(sprite_id))
    return absl::AlreadyExistsError("Profile already exists.");

  sprites_[sprite_id] = sprite;
  active_sprite_id_ = sprite_id;
  return absl::OkStatus();
}

absl::Status SpriteObject::RemoveSprite(uint16_t sprite_id) {
  auto profile_iter = sprites_.find(sprite_id);
  if (profile_iter == sprites_.end())
    return absl::NotFoundError("Profile not found.");

  sprites_.erase(profile_iter);
  return absl::OkStatus();
}

absl::Status SpriteObject::RemoveSpriteByType(uint16_t sprite_type) {
  for (auto &it : sprites_) {
    if (it.second->GetType() != sprite_type) continue;
    sprites_.erase(it.first);
    return absl::OkStatus();
  }

  return absl::NotFoundError("Profile not found.");
}

absl::Status SpriteObject::SetActiveSprite(uint16_t sprite_id) {
  if (!sprites_.contains(sprite_id))
    return absl::NotFoundError("Profile not found.");

  active_sprite_id_ = sprite_id;
  active_sprite_ticks_ = 0;
  return absl::OkStatus();
}

absl::Status SpriteObject::SetActiveSpriteByType(uint16_t sprite_type) {
  const SpriteInterface *sprite = nullptr;
  for (const auto &it : sprites_) {
    if (it.second->GetType() != sprite_type) continue;
    sprite = it.second;
  }
  if (sprite == nullptr) return absl::NotFoundError("Profile not found.");

  active_sprite_id_ = sprite->GetId();
  active_sprite_ticks_ = 0;
  return absl::OkStatus();
}

const SpriteInterface *SpriteObject::GetActiveSprite() const {
  return sprites_.at(active_sprite_id_);
}

uint8_t SpriteObject::GetActiveSpriteTicks() const {
  return active_sprite_ticks_;
}

uint64_t SpriteObject::GetActiveSpriteCycles() const {
  return active_sprite_cycles_;
}

uint16_t SpriteObject::GetActiveSpriteIndex() const {
  if (active_sprite_id_ == 0) return 0;

  const SpriteInterface *sprite = sprites_.at(active_sprite_id_);
  int ticks_per_sprite = sprite->GetConfig()->ticks_per_sprite;
  if (ticks_per_sprite == 0) return 0;

  return GetActiveSpriteTicks() / ticks_per_sprite;
}

void SpriteObject::Update() {
  if (active_sprite_id_ == 0) return;

  const SpriteInterface *sprite = sprites_.at(active_sprite_id_);
  const SpriteConfig *sprite_config = sprite->GetConfig();

  int ticks_per_cycle = sprite_config->ticks_per_sprite * sprite_config->size();

  active_sprite_ticks_++;
  if (ticks_per_cycle == 0) return;

  active_sprite_cycles_ += (active_sprite_ticks_ / ticks_per_cycle);
  active_sprite_ticks_ = active_sprite_ticks_ % ticks_per_cycle;
};

void SpriteObject::ResetSprite() {
  active_sprite_ticks_ = 0;
  active_sprite_cycles_ = 0;
};

}  // namespace zebes
