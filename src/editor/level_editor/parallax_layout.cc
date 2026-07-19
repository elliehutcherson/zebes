#include "editor/level_editor/parallax_layout.h"

#include <algorithm>
#include <cmath>

namespace zebes {

VisibleWorldBounds CalculateVisibleWorldBounds(const Camera& camera) {
  const double half_width = camera.viewport_width / (2.0 * camera.zoom);
  const double half_height = camera.viewport_height / (2.0 * camera.zoom);
  return {
      .min = {camera.position.x - half_width, camera.position.y - half_height},
      .max = {camera.position.x + half_width, camera.position.y + half_height},
  };
}

const ParallaxZone* FindParallaxZoneById(const std::vector<ParallaxZone>& zones, int zone_id) {
  auto it = std::find_if(zones.begin(), zones.end(),
                         [zone_id](const ParallaxZone& zone) { return zone.id == zone_id; });
  return it == zones.end() ? nullptr : &*it;
}

std::optional<ActiveParallaxZone> ResolveActiveParallaxZone(
    const std::vector<ParallaxZone>& zones, Vec reference_point) {
  for (auto it = zones.rbegin(); it != zones.rend(); ++it) {
    if (reference_point.x >= it->min_point.x && reference_point.x < it->max_point.x &&
        reference_point.y >= it->min_point.y && reference_point.y < it->max_point.y) {
      return ActiveParallaxZone{
          .zone_id = it->id,
          .theme_id = it->theme_id,
      };
    }
  }
  return std::nullopt;
}

std::optional<CameraFrame> CalculateCameraFrame(VisibleWorldBounds bounds, int viewport_width,
                                                int viewport_height, double padding_fraction) {
  const double bounds_width = bounds.max.x - bounds.min.x;
  const double bounds_height = bounds.max.y - bounds.min.y;
  if (bounds_width <= 0 || bounds_height <= 0 || viewport_width <= 0 || viewport_height <= 0 ||
      padding_fraction < 0 || padding_fraction >= 0.5) {
    return std::nullopt;
  }

  const double usable_fraction = 1.0 - 2.0 * padding_fraction;
  const double zoom_x = viewport_width * usable_fraction / bounds_width;
  const double zoom_y = viewport_height * usable_fraction / bounds_height;
  return CameraFrame{
      .position = {(bounds.min.x + bounds.max.x) / 2.0,
                   (bounds.min.y + bounds.max.y) / 2.0},
      .zoom = std::min(zoom_x, zoom_y),
  };
}

std::optional<CameraFrame> CalculateConstrainedCameraFrame(
    VisibleWorldBounds target_bounds, VisibleWorldBounds world_bounds, int viewport_width,
    int viewport_height, double padding_fraction) {
  std::optional<CameraFrame> frame = CalculateCameraFrame(
      target_bounds, viewport_width, viewport_height, padding_fraction);
  if (!frame.has_value()) return std::nullopt;

  const double world_width = world_bounds.max.x - world_bounds.min.x;
  const double world_height = world_bounds.max.y - world_bounds.min.y;
  const double horizontal_clearance =
      std::min(frame->position.x - world_bounds.min.x,
               world_bounds.max.x - frame->position.x);
  const double vertical_clearance =
      std::min(frame->position.y - world_bounds.min.y,
               world_bounds.max.y - frame->position.y);
  if (world_width <= 0 || world_height <= 0 || horizontal_clearance <= 0 ||
      vertical_clearance <= 0) {
    return std::nullopt;
  }

  const double minimum_horizontal_zoom = viewport_width / (2.0 * horizontal_clearance);
  const double minimum_vertical_zoom = viewport_height / (2.0 * vertical_clearance);
  frame->zoom =
      std::max({frame->zoom, minimum_horizontal_zoom, minimum_vertical_zoom});
  return frame;
}

std::optional<ParallaxLayout> CalculateParallaxLayout(const Camera& camera,
                                                      const ParallaxLayer& layer,
                                                      int texture_width,
                                                      int texture_height) {
  if (camera.zoom <= 0 || camera.viewport_width <= 0 || camera.viewport_height <= 0 ||
      texture_width <= 0 || texture_height <= 0 || layer.base_scale <= 0) {
    return std::nullopt;
  }

  ParallaxLayout layout;
  layout.origin = camera.ParallaxWorldOrigin(layer.scroll_factor, layer.offset);
  layout.tile_width = static_cast<double>(texture_width) * layer.base_scale;
  layout.tile_height = static_cast<double>(texture_height) * layer.base_scale;

  const VisibleWorldBounds visible = CalculateVisibleWorldBounds(camera);
  layout.first_column =
      static_cast<int>(std::floor((visible.min.x - layout.origin.x) / layout.tile_width));
  layout.last_column =
      static_cast<int>(std::floor((visible.max.x - layout.origin.x) / layout.tile_width));
  layout.first_row =
      static_cast<int>(std::floor((visible.min.y - layout.origin.y) / layout.tile_height));
  layout.last_row =
      static_cast<int>(std::floor((visible.max.y - layout.origin.y) / layout.tile_height));

  if (!layer.repeat_x) {
    layout.first_column = 0;
    layout.last_column = 0;
  }
  if (!layer.repeat_y) {
    layout.first_row = 0;
    layout.last_row = 0;
  }

  return layout;
}

}  // namespace zebes
