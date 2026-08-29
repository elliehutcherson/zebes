#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "game/game_scene.h"
#include "objects/game_view.h"

struct SDL_Renderer;

namespace zebes {

class SdlWrapper;

// Main-thread SDL presentation adapter for a platform-neutral game scene.
// Create configures the renderer's logical game resolution; Render clears,
// draws the stable scene passes, and presents once.
class SdlGameRenderer {
 public:
  static absl::StatusOr<std::unique_ptr<SdlGameRenderer>> Create(SdlWrapper& sdl,
                                                                 GameViewSize game_view);

  absl::Status Render(const GameSceneFrame& frame) const;

 private:
  SdlGameRenderer(SDL_Renderer& renderer, GameViewSize game_view);

  SDL_Renderer& renderer_;
  GameViewSize game_view_;
};

}  // namespace zebes
