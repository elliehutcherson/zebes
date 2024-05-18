#pragma once

#include "SDL_rect.h"
#include "SDL_render.h"

#include "absl/status/status.h" 

#include "camera.h"
#include "config.h"

namespace zebes {

class Map {
  public:
    Map(const GameConfig* config, const Camera* camera);
    ~Map() = default;
    // Initialize Player
    absl::Status Init(SDL_Renderer* renderer);
    // Draw player.
    void Render();
    // Move player.
    void Move();

  private:
    const GameConfig* config_;
    const Camera* camera_;
    SDL_Texture* texture_;
    SDL_Rect src_rect_;
    SDL_Rect dst_rect_;
};

}  // namespace zebes