#pragma once

#include "absl/status/status.h"
#include "game/game_scene.h"

namespace zebes {

// Platform-neutral presentation boundary. Implementations may use SDL or a
// headless recorder, but the game loop only submits complete scene frames.
class GameRenderer {
 public:
  virtual ~GameRenderer() = default;
  virtual absl::Status Render(const GameSceneFrame& frame) const = 0;
};

}  // namespace zebes
