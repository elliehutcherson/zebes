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

  // Destructor.
  ~Sprite() = default;

  void Update() override;

  void Reset() override;

  SDL_Texture *GetTexture() override;

  SDL_Rect *GetSource() override;

  const SDL_Rect *GetSource(int index) const override;

  uint16_t GetId() const override;

  uint16_t GetType() const override;

  const SpriteConfig *GetConfig() const override;

  const SubSprite *GetSubSprite(int index) const override;

  const SubSprite *CurrentSubSprite() const override;

  int GetIndex() const override;

  uint16_t GetCycle() const override;

  int GetTicks() const override;

 private:
  // Contructor, use Create method instead.
  Sprite(SpriteConfig sprite_config);
  // Load the texture.
  absl::Status Init(SDL_Renderer *renderer);

  int ticks_ = 0;
  int index_ = 0;
  uint16_t cycle_ = 0;
  SpriteConfig sprite_config_;

  SDL_Texture *texture_;
  std::vector<SDL_Rect> sources_;
};

}  // namespace zebes