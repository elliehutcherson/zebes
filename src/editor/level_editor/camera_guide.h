#pragma once

#include <optional>

#include "objects/game_view.h"
#include "objects/vec.h"

namespace zebes {

// World-space geometry used to preview the game camera inside the editor.
struct CameraGuide {
  Vec center;
  Vec min;
  Vec max;
};

// Calculates the game view centered at camera_position. Returns nullopt when
// either configured dimension is not positive.
std::optional<CameraGuide> CalculateCameraGuide(Vec camera_position,
                                                const GameViewSize& game_view);

}  // namespace zebes
