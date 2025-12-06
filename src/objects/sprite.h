#pragma once

#include <cstdint>

#include "SDL_render.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/sprite_interface.h"

namespace zebes {

class Sprite : public SpriteInterface {
 public:
  struct Options {
    SpriteConfig config;
    SDL_Renderer* renderer;
  };

  // Create fully initialized sprite.
  static absl::StatusOr<std::unique_ptr<Sprite>> Create(Options options);

  ~Sprite() = default;

  SDL_Texture* GetTexture() const override;

  SDL_Texture* GetTextureCopy(int index) const override;

  const SDL_Rect* GetSource(int index) const override;

  uint16_t GetId() const override;

  uint16_t GetType() const override;

  const SpriteConfig* GetConfig() const override;

  const SpriteFrame* GetSpriteFrame(int index) const override;

 private:
  // Contructor, use Create method instead.
  Sprite(SpriteConfig sprite_config);

  // Load the texture.
  absl::Status Init(SDL_Renderer* renderer);

  SpriteConfig sprite_config_;
  SDL_Texture* texture_;
  std::vector<SDL_Rect> sources_;
  std::vector<SDL_Texture*> textures_copies_;
};

}  // namespace zebes