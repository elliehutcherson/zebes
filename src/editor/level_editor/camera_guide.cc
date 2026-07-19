#include "editor/level_editor/camera_guide.h"

namespace zebes {

std::optional<CameraGuide> CalculateCameraGuide(Vec camera_position,
                                                const GameViewSize& game_view) {
  if (game_view.width <= 0 || game_view.height <= 0) return std::nullopt;

  const double half_width = game_view.width / 2.0;
  const double half_height = game_view.height / 2.0;
  return CameraGuide{
      .center = camera_position,
      .min = {camera_position.x - half_width, camera_position.y - half_height},
      .max = {camera_position.x + half_width, camera_position.y + half_height},
  };
}

}  // namespace zebes
