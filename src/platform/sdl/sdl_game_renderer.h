#pragma once

#include <memory>

#include "SDL_render.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/sdl_wrapper.h"
#include "game/game_renderer.h"
#include "game/game_scene.h"
#include "objects/game_view.h"

namespace zebes {

// Main-thread SDL presentation adapter for a platform-neutral game scene.
// Create configures the renderer's logical game resolution; Render clears,
// draws the stable scene passes, and presents once.
class SdlGameRenderer final : public GameRenderer {
 public:
  static absl::StatusOr<std::unique_ptr<SdlGameRenderer>> Create(SdlWrapper& sdl,
                                                                 GameViewSize game_view);

  absl::Status Render(const GameSceneFrame& frame) const override;

 private:
  SdlGameRenderer(SDL_Renderer& renderer, GameViewSize game_view);

  SDL_Renderer& renderer_;
  GameViewSize game_view_;
};

}  // namespace zebes
