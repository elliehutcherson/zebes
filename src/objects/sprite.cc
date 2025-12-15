#include "objects/sprite.h"

#include "SDL_image.h"
#include "SDL_render.h"
#include "common/status_macros.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Sprite>> Sprite::Create(Options options) {
  std::unique_ptr<Sprite> sprite(new Sprite(std::move(options.meta)));
  absl::Status result = sprite->Init(options.sdl);
  if (!result.ok()) return result;

  return sprite;
}

Sprite::Sprite(SpriteMeta sprite_meta) : sprite_meta_(sprite_meta) {
  for (const SpriteFrame& sprite_frame : sprite_meta_.sprite_frames) {
    SDL_Rect source{.x = sprite_frame.texture_x,
                    .y = sprite_frame.texture_y,
                    .w = sprite_frame.texture_w,
                    .h = sprite_frame.texture_h};
    sources_.push_back(source);
  }
}

absl::Status Sprite::Init(SdlWrapper* sdl) {
  if (sdl == nullptr) {
    return absl::InvalidArgumentError("SdlWrapper must not be null");
  }

  if (sprite_meta_.size() <= 0) {
    // Should we error if no frames? Maybe allow empty sprite?
    // Legacy code errored.
    // Let's assume user wants to allow loading metadata even if invalid?
    // But returning error here fails creation.
    // If sprite_frames is empty, user can add them in editor.
    // The previous code returned InternalError. I'll stick to error for now.
    // return absl::InternalError("Sprite Meta has invalid size.");
    // Wait, size() is vector size. Empty vector is valid in JSON.
    // So failing here makes it impossible to create new empty sprite.
    // I'll disable this check or allow empty.
    // Actually, `CreateSprite` passes valid meta. New sprite has empty frames.
    // So strict check prevents creation of new sprite.
    // I will log warning but continue.
    // LOG(WARNING) << "Sprite initialized with no frames.";
  }

  ASSIGN_OR_RETURN(texture_, sdl->CreateTexture(sprite_meta_.texture_path));

  // Texture copies for rendering in creator hud.
  SDL_Renderer* renderer = sdl->GetRenderer();
  if (renderer != nullptr && !sprite_meta_.sprite_frames.empty()) {
    for (int i = 0; i < sprite_meta_.size(); i++) {
      SDL_Texture* texture = SDL_CreateTexture(
          renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
          sprite_meta_.sprite_frames[i].render_w, sprite_meta_.sprite_frames[i].render_h);

      if (texture) {
        SDL_SetRenderTarget(renderer, texture);
        SDL_RenderCopy(renderer, texture_, GetSource(i), nullptr);
        SDL_SetRenderTarget(renderer, nullptr);
        textures_copies_.push_back(texture);
      }
    }
  }

  return absl::OkStatus();
}

SDL_Texture* Sprite::GetTexture() const { return texture_; }

SDL_Texture* Sprite::GetTextureCopy(int index) const {
  if (textures_copies_.size() <= index) return nullptr;
  return textures_copies_[index];
}

const SDL_Rect* Sprite::GetSource(int index) const { return &sources_[index]; }

std::string Sprite::GetId() const { return sprite_meta_.id; }

uint16_t Sprite::GetType() const { return sprite_meta_.type; }

const SpriteMeta* Sprite::GetMeta() const { return &sprite_meta_; }

const SpriteFrame* Sprite::GetSpriteFrame(int index) const {
  return &sprite_meta_.sprite_frames[index];
}

}  // namespace zebes
