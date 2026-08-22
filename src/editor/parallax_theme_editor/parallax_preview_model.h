#pragma once

#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "objects/game_view.h"
#include "objects/level.h"
#include "objects/vec.h"

namespace zebes {

struct CameraWorldBounds {
  Vec min;
  Vec max;
};

// Inclusive endpoints for the camera center, not world content bounds.
struct CameraCenterRoute {
  Vec min;
  Vec max;
};

struct ParallaxThemeUsage {
  std::string level_id;
  std::string level_name;
  int zone_id = -1;
  std::string zone_name;
  CameraCenterRoute route;
  CameraWorldBounds world;
};

std::vector<ParallaxThemeUsage> FindParallaxThemeUsages(const std::vector<Level>& levels,
                                                        const std::string& theme_id);

// Intersects an authored camera-center route with centers actually reachable
// inside optional world bounds at one zoom. It fails clearly when inputs are
// invalid or the camera cannot enter the route at that zoom.
absl::StatusOr<CameraCenterRoute> ResolveCameraCenterRoute(
    CameraCenterRoute route, const GameViewSize& game_view, double zoom,
    std::optional<CameraWorldBounds> world = std::nullopt);

Vec InterpolateCameraCenter(CameraCenterRoute route, double x_progress, double y_progress);

}  // namespace zebes
