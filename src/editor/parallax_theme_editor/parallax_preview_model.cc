#include "editor/parallax_theme_editor/parallax_preview_model.h"

#include <algorithm>
#include <cmath>

#include "absl/status/status.h"

namespace zebes {
namespace {

bool Finite(Vec value) { return std::isfinite(value.x) && std::isfinite(value.y); }

bool Previewable(const ParallaxElement& element) {
  return element.id >= 0 && !element.texture_id.empty() && Finite(element.position) &&
         std::isfinite(element.scale) && element.scale > 0.0f;
}

bool Previewable(const ParallaxLayer& layer) {
  return Finite(layer.scroll_factor) && Finite(layer.offset) && Finite(layer.repeat_period) &&
         layer.repeat_period.x >= 0.0 && layer.repeat_period.y >= 0.0;
}

}  // namespace

bool IsRecoverableParallaxPreviewError(const absl::Status& status) {
  return status.code() == absl::StatusCode::kResourceExhausted;
}

CameraCenterRoute EnsureNavigableManualCameraRoute(CameraCenterRoute route,
                                                   const GameViewSize& game_view) {
  if (route.min != route.max) return route;
  route.max = {
      route.min.x + game_view.width,
      route.min.y + game_view.height,
  };
  return route;
}

absl::StatusOr<CameraCenterRoute> CalculateContentCameraRoute(const ParallaxTheme& theme,
                                                              const GameViewSize& game_view) {
  if (!game_view.IsValid()) {
    return absl::InvalidArgumentError("content camera route requires a valid game view");
  }
  const Vec fallback{game_view.width / 2.0, game_view.height / 2.0};
  CameraCenterRoute route{.min = fallback, .max = fallback};
  for (const ParallaxLayer& layer : theme.layers) {
    if (!Finite(layer.scroll_factor) || !Finite(layer.offset)) {
      return absl::InvalidArgumentError("content camera route requires finite layer geometry");
    }
    for (const ParallaxElement& element : layer.elements) {
      if (!Finite(element.position)) {
        return absl::InvalidArgumentError("content camera route requires finite element positions");
      }
      if (layer.scroll_factor.x != 0.0) {
        const double center =
            fallback.x + layer.offset.x + element.position.x / layer.scroll_factor.x;
        if (!std::isfinite(center)) {
          return absl::InvalidArgumentError("horizontal content camera route is not finite");
        }
        route.min.x = std::min(route.min.x, center);
        route.max.x = std::max(route.max.x, center);
      }
      if (layer.scroll_factor.y != 0.0) {
        const double center =
            fallback.y + layer.offset.y + element.position.y / layer.scroll_factor.y;
        if (!std::isfinite(center)) {
          return absl::InvalidArgumentError("vertical content camera route is not finite");
        }
        route.min.y = std::min(route.min.y, center);
        route.max.y = std::max(route.max.y, center);
      }
    }
  }
  return EnsureNavigableManualCameraRoute(route, game_view);
}

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

absl::StatusOr<Vec> CalculateCameraTravel(CameraCenterRoute route, Vec camera_center) {
  if (!Finite(route.min) || !Finite(route.max) || route.min.x > route.max.x ||
      route.min.y > route.max.y || !Finite(camera_center)) {
    return absl::InvalidArgumentError("camera travel requires a valid route and center");
  }
  const auto progress = [](double minimum, double maximum, double center) {
    if (minimum == maximum) return 0.0;
    return std::clamp((center - minimum) / (maximum - minimum), 0.0, 1.0);
  };
  return Vec{
      progress(route.min.x, route.max.x, camera_center.x),
      progress(route.min.y, route.max.y, camera_center.y),
  };
}

ParallaxPreviewTheme BuildParallaxPreviewTheme(const ParallaxTheme& draft) {
  ParallaxPreviewTheme preview;
  preview.theme.id = draft.id;
  preview.theme.name = draft.name;
  preview.theme.layers.reserve(draft.layers.size());
  preview.source_layer_indices.reserve(draft.layers.size());
  for (int layer_index = 0; layer_index < static_cast<int>(draft.layers.size()); ++layer_index) {
    const ParallaxLayer& source_layer = draft.layers[layer_index];
    if (!Previewable(source_layer)) {
      preview.omitted_elements += source_layer.elements.size();
      continue;
    }
    ParallaxLayer layer = source_layer;
    std::erase_if(layer.elements, [&preview](const ParallaxElement& element) {
      if (Previewable(element)) return false;
      ++preview.omitted_elements;
      return true;
    });
    if (layer.elements.empty()) continue;
    preview.theme.layers.push_back(std::move(layer));
    preview.source_layer_indices.push_back(layer_index);
  }
  return preview;
}

std::optional<int> FindPreviewLayerIndex(const ParallaxPreviewTheme& preview,
                                         int source_layer_index) {
  const auto found = std::find(preview.source_layer_indices.begin(),
                               preview.source_layer_indices.end(), source_layer_index);
  if (found == preview.source_layer_indices.end()) return std::nullopt;
  return static_cast<int>(found - preview.source_layer_indices.begin());
}

}  // namespace zebes
