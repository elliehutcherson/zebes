#include "sprite_manager.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "SDL_image.h"
#include "SDL_render.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "camera.h"
#include "object.h"
#include "sprite.h"
#include "sprite_interface.h"
#include "sprite_object.h"
#include "status_macros.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<SpriteManager>> SpriteManager::Create(
    const Options &options) {
  if (options.config == nullptr)
    return absl::InvalidArgumentError("GameConfig is null.");
  if (options.renderer == nullptr)
    return absl::InvalidArgumentError("SDL_Renderer is null.");
  if (options.camera == nullptr)
    return absl::InvalidArgumentError("Camera is null.");
  if (options.db == nullptr)
    return absl::InvalidArgumentError("DbInterface is null.");

  std::unique_ptr<SpriteManager> sprite_manager(new SpriteManager(options));
  RETURN_IF_ERROR(sprite_manager->Init());

  return sprite_manager;
}

SpriteManager::SpriteManager(const Options &options)
    : config_(options.config),
      renderer_(options.renderer),
      camera_(options.camera),
      db_(options.db) {}

absl::Status SpriteManager::Init() {
  ASSIGN_OR_RETURN(std::vector<SpriteConfig> sprite_configs,
                   db_->GetAllSprites());

  for (SpriteConfig &sprite_config : sprite_configs) {
    ASSIGN_OR_RETURN(sprite_config.sub_sprites,
                     db_->GetSubSprites(sprite_config.id));

    RETURN_IF_ERROR(InitializeSprite(sprite_config));
  }

  return absl::OkStatus();
}

absl::Status SpriteManager::InitializeSprite(SpriteConfig sprite_config) {
  if (sprite_config.size() <= 0) {
    return absl::InternalError(absl::StrCat(
        __func__,
        "Sprite Config has invalid size, sprite_id: ", sprite_config.id));
  }

  SDL_Texture *texture = nullptr;
  auto texture_iter = path_to_texture_.find(sprite_config.texture_path);
  LOG(INFO) << __func__ << ": "
            << absl::StrFormat("sprite_config.texture_path = %s",
                               sprite_config.texture_path);

  LOG(INFO) << __func__ << ": "
            << absl::StrFormat("sprite_config.type = %d",
                               static_cast<int>(sprite_config.type));

  if (texture_iter == path_to_texture_.end()) {
    SDL_Surface *tmp_surface = IMG_Load(sprite_config.texture_path.c_str());
    if (tmp_surface == nullptr)
      return absl::AbortedError("Failed to create tmp_surface.");

    texture = SDL_CreateTextureFromSurface(renderer_, tmp_surface);
    if (texture == nullptr)
      return absl::AbortedError("Failed to create texture.");

    SDL_FreeSurface(tmp_surface);
    path_to_texture_.insert({sprite_config.texture_path, texture});
  }

  absl::StatusOr<std::unique_ptr<Sprite>> sprite =
      Sprite::Create({.config = sprite_config, .renderer = renderer_});
  if (!sprite.ok()) return sprite.status();

  auto [it, inserted] = sprites_.emplace(sprite_config.id, std::move(*sprite));
  if (!inserted) {
    return absl::AbortedError("Failed to insert sprite.");
  }

  type_to_sprites_.insert({sprite_config.type, it->second.get()});

  return absl::OkStatus();
}

std::vector<uint16_t> SpriteManager::GetAllSpriteIds() const {
  std::vector<uint16_t> sprite_ids;
  for (auto &[id, sprite] : sprites_) {
    sprite_ids.push_back(id);
  }
  return sprite_ids;
}

absl::StatusOr<const Sprite *> SpriteManager::GetSprite(
    uint16_t sprite_id) const {
  auto sprite_iter = sprites_.find(sprite_id);
  if (sprite_iter == sprites_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Sprite not found, sprite_id: ", sprite_id));
  }
  return sprite_iter->second.get();
}

absl::StatusOr<const Sprite *> SpriteManager::GetSpriteByType(
    uint16_t sprite_type) const {
  auto it = type_to_sprites_.find(sprite_type);
  if (it == type_to_sprites_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Sprite not found, sprite_type: ", sprite_type));
  }
  return it->second;
}

absl::Status SpriteManager::AddSpriteObject(SpriteObjectInterface *object) {
  if (object == nullptr)
    return absl::InvalidArgumentError("SpriteObjectInterface is null.");
  sprite_objects_.push_back(object);
  return absl::OkStatus();
}

absl::Status SpriteManager::Render(uint16_t sprite_id, int index,
                                   Point position) {
  auto it = sprites_.find(sprite_id);
  if (it == sprites_.end())
    return absl::InvalidArgumentError("Type not found.");

  Sprite *sprite = it->second.get();
  const SpriteConfig *config = sprite->GetConfig();

  if (index >= config->size())
    return absl::InvalidArgumentError("Index out of bounds.");

  const SubSprite *sub_sprite = sprite->GetSubSprite(index);

  const SDL_Rect dst_rect{
      .x = position.x_floor() + sub_sprite->render_offset_x,
      .y = position.y_floor() + sub_sprite->render_offset_y,
      .w = sub_sprite->render_w,
      .h = sub_sprite->render_h,
  };
  const SDL_Rect *src_rect = sprite->GetSource(index);

  camera_->Render(sprite->GetTexture(), src_rect, &dst_rect);

  return absl::OkStatus();
}

// const std::vector<SDL_Texture *> &SpriteManager::GetAllSpritesAsTextures() {
//   if (!experiment_textures_.empty()) return experiment_textures_;

//   for (auto &[_, sprite] : sprites_) {
//     for (int i = 0; i < sprite->GetConfig()->size(); i++) {
//       SDL_Texture *target_texture = SDL_CreateTexture(
//           renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
//           config_->tiles.render_width(), config_->tiles.render_height());

//       SDL_SetRenderTarget(renderer_, target_texture);
//       SDL_RenderCopy(renderer_, sprite->GetTexture(), sprite->GetSource(i),
//                      nullptr);
//       SDL_SetRenderTarget(renderer_, nullptr);

//       experiment_textures_.push_back(target_texture);
//     }
//   }

//   return experiment_textures_;
// }

}  // namespace zebes
