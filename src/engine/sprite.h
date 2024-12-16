#pragma once

#include <cstdint>

#include "SDL_render.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "sprite_interface.h"

namespace zebes {

class Sprite : public SpriteInterface {
 public:
  struct Options {
    SpriteConfig config;
    SDL_Renderer *renderer;
  };

  // Create fully initialized sprite.
  static absl::StatusOr<std::unique_ptr<Sprite>> Create(Options options);

  ~Sprite() = default;

  SDL_Texture *GetTexture() override;

  const SDL_Rect *GetSource(int index) const override;

  uint16_t GetId() const override;

  uint16_t GetType() const override;

  const SpriteConfig *GetConfig() const override;

  const SubSprite *GetSubSprite(int index) const override;

 private:
  // Contructor, use Create method instead.
  Sprite(SpriteConfig sprite_config);

  // Load the texture.
  absl::Status Init(SDL_Renderer *renderer);

  SpriteConfig sprite_config_;
  SDL_Texture *texture_;
  std::vector<SDL_Rect> sources_;
};

}  // namespace zebes