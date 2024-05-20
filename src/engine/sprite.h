#pragma once

#include "SDL_render.h"

#include "absl/status/status.h"

#include "config.h"

namespace zebes {

class Sprite {
public:
  // Contructor.
  Sprite(SpriteConfig sprite_config);
  // Destructor.
  ~Sprite() = default;
  // Load the texture.
  absl::Status Init(SDL_Renderer *renderer);
  // Update ticks
  void Update();
  // Get Source texture rectangle.
  void Reset();
  // Get Source texture.
  SDL_Texture *GetTexture();
  // Get Source texture rectangle.
  SDL_Rect *GetSource();
  // Get Source texture rectangle.
  const SDL_Rect *GetSource(int index) const;
  // Get SpriteType
  SpriteType GetType() const;
  // Get config.
  const SpriteConfig *GetConfig() const;
  // Get SubSprite.
  const SubSprite *GetSubSprite(int index) const;
  // Return the subsprite using the internal index.
  const SubSprite *CurrentSubSprite() const;
  // Get the index for the current cycle.
  int GetIndex() const;
  // Get number of cycles, or the number of times a full
  // animation has been played since reset.
  uint64_t GetCycle() const;
  // Get the ticks for the current cycle.
  int GetTicks() const;

private:
  int ticks_ = 0;
  int index_ = 0;
  uint64_t cycle_ = 0;
  SpriteConfig sprite_config_;

  SDL_Texture *texture_;
  std::vector<SDL_Rect> sources_;
};

} // namespace zebes