#include "sprite.h"

#include "SDL_image.h"
#include "SDL_render.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Sprite>> Sprite::Create(Options options) {
  std::unique_ptr<Sprite> sprite(new Sprite(std::move(options.config)));
  absl::Status result = sprite->Init(options.renderer);
  if (!result.ok()) return result;

  return sprite;
}

Sprite::Sprite(SpriteConfig sprite_config) : sprite_config_(sprite_config) {
  for (const SubSprite &sub_sprite : sprite_config_.sub_sprites) {
    SDL_Rect source{.x = sub_sprite.texture_x,
                    .y = sub_sprite.texture_y,
                    .w = sub_sprite.texture_w,
                    .h = sub_sprite.texture_h};
    sources_.push_back(source);
  }
}

absl::Status Sprite::Init(SDL_Renderer *renderer) {
  if (sprite_config_.size() <= 0) {
    return absl::InternalError("Sprite Config has invalid size.");
  }
  SDL_Surface *tmp_surface = IMG_Load(sprite_config_.texture_path.c_str());
  if (tmp_surface == nullptr) {
    return absl::AbortedError("Failed to create tmp_surface.");
  }
  texture_ = SDL_CreateTextureFromSurface(renderer, tmp_surface);
  if (texture_ == nullptr) {
    return absl::AbortedError("Failed to create player.");
  }
  SDL_FreeSurface(tmp_surface);

  // Texture copies for rendering in creator hud.
  for (int i = 0; i < sprite_config_.size(); i++) {
    SDL_Texture *texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        sprite_config_.sub_sprites[i].render_w,
        sprite_config_.sub_sprites[i].render_h);

    SDL_SetRenderTarget(renderer, texture);
    SDL_RenderCopy(renderer, texture_, GetSource(i), nullptr);
    SDL_SetRenderTarget(renderer, nullptr);

    textures_copies_.push_back(texture);
  }

  return absl::OkStatus();
}

SDL_Texture *Sprite::GetTexture() const { return texture_; }

SDL_Texture *Sprite::GetTextureCopy(int index) const {
  if (textures_copies_.size() <= index) return nullptr;
  return textures_copies_[index];
}

const SDL_Rect *Sprite::GetSource(int index) const { return &sources_[index]; }

uint16_t Sprite::GetId() const { return sprite_config_.id; }

uint16_t Sprite::GetType() const { return sprite_config_.type; }

const SpriteConfig *Sprite::GetConfig() const { return &sprite_config_; }

const SubSprite *Sprite::GetSubSprite(int index) const {
  return &sprite_config_.sub_sprites[index];
}

}  // namespace zebes
