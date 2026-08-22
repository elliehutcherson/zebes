#include "editor/parallax_theme_editor/parallax_preview_model.h"

#include <algorithm>
#include <cmath>

#include "absl/status/status.h"

namespace zebes {
namespace {

bool Finite(Vec value) { return std::isfinite(value.x) && std::isfinite(value.y); }

}  // namespace

std::vector<ParallaxThemeUsage> FindParallaxThemeUsages(const std::vector<Level>& levels,
                                                        const std::string& theme_id) {
  std::vector<ParallaxThemeUsage> usages;
  if (theme_id.empty()) return usages;
  for (const Level& level : levels) {
    for (const ParallaxZone& zone : level.zones) {
      if (zone.theme_id != theme_id) continue;
      usages.push_back({
          .level_id = level.id,
          .level_name = level.name,
          .zone_id = zone.id,
          .zone_name = zone.name,
          .route = {.min = zone.min_point, .max = zone.max_point},
          .world = {.min = {0, 0}, .max = {level.width, level.height}},
      });
    }
  }
  return usages;
}

absl::StatusOr<CameraCenterRoute> ResolveCameraCenterRoute(CameraCenterRoute route,
                                                           const GameViewSize& game_view,
                                                           double zoom,
                                                           std::optional<CameraWorldBounds> world) {
  if (!Finite(route.min) || !Finite(route.max) || route.min.x > route.max.x ||
      route.min.y > route.max.y || !game_view.IsValid() || !std::isfinite(zoom) || zoom <= 0.0) {
    return absl::InvalidArgumentError("camera center route requires valid geometry and zoom");
  }
  if (!world.has_value()) return route;
  if (!Finite(world->min) || !Finite(world->max) || world->min.x >= world->max.x ||
      world->min.y >= world->max.y) {
    return absl::InvalidArgumentError("camera world bounds must have positive finite dimensions");
  }

  const Vec half_view{
      game_view.width / (2.0 * zoom),
      game_view.height / (2.0 * zoom),
  };
  const CameraCenterRoute reachable_world{
      .min = {world->min.x + half_view.x, world->min.y + half_view.y},
      .max = {world->max.x - half_view.x, world->max.y - half_view.y},
  };
  if (reachable_world.min.x > reachable_world.max.x ||
      reachable_world.min.y > reachable_world.max.y) {
    return absl::FailedPreconditionError("game view does not fit inside the selected level");
  }

  CameraCenterRoute resolved{
      .min = {std::max(route.min.x, reachable_world.min.x),
              std::max(route.min.y, reachable_world.min.y)},
      .max = {std::min(route.max.x, reachable_world.max.x),
              std::min(route.max.y, reachable_world.max.y)},
  };
  if (resolved.min.x > resolved.max.x || resolved.min.y > resolved.max.y) {
    return absl::FailedPreconditionError(
        "camera center cannot enter the selected zone at this zoom");
  }
  return resolved;
}

Vec InterpolateCameraCenter(CameraCenterRoute route, double x_progress, double y_progress) {
  x_progress = std::clamp(x_progress, 0.0, 1.0);
  y_progress = std::clamp(y_progress, 0.0, 1.0);
  return {
      route.min.x + (route.max.x - route.min.x) * x_progress,
      route.min.y + (route.max.y - route.min.y) * y_progress,
  };
}

}  // namespace zebes
