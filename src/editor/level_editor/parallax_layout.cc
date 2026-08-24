#include "editor/level_editor/parallax_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

struct CompositionGeometry {
  WorldRect bounds;
  std::map<int, WorldRect> element_bounds;
};

using CopyRange = std::pair<int, int>;

bool IsFinite(Vec value) { return std::isfinite(value.x) && std::isfinite(value.y); }

bool IsValidElementSize(const ParallaxElementSize& size) {
  return size.element_id >= 0 && size.width > 0 && size.height > 0;
}

bool IsValidElementGeometry(const ParallaxElement& element) {
  return IsFinite(element.position) && std::isfinite(element.scale) && element.scale > 0.0f;
}

bool Intersects(WorldRect left, VisibleWorldBounds right) {
  return left.max.x > right.min.x && left.min.x < right.max.x && left.max.y > right.min.y &&
         left.min.y < right.max.y;
}

absl::Status ValidateLayoutInputs(const Camera& camera, const ParallaxLayer& layer) {
  if (camera.zoom <= 0 || camera.viewport_width <= 0 || camera.viewport_height <= 0 ||
      !std::isfinite(camera.zoom) || !IsFinite(camera.position)) {
    return absl::InvalidArgumentError("parallax layout camera is invalid");
  }
  if (!IsFinite(layer.scroll_factor) || !IsFinite(layer.offset) || !IsFinite(layer.repeat_period) ||
      layer.repeat_period.x < 0.0 || layer.repeat_period.y < 0.0 || layer.elements.empty()) {
    return absl::InvalidArgumentError("parallax layer geometry is invalid");
  }
  return absl::OkStatus();
}

absl::StatusOr<CompositionGeometry> BuildCompositionGeometry(
    const ParallaxLayer& layer, const std::vector<ParallaxElementSize>& element_sizes) {
  if (layer.elements.empty()) {
    return absl::InvalidArgumentError("parallax composition must contain an element");
  }
  std::map<int, ParallaxElementSize> sizes;
  for (const ParallaxElementSize& size : element_sizes) {
    if (!IsValidElementSize(size) || !sizes.emplace(size.element_id, size).second) {
      return absl::InvalidArgumentError("parallax element sizes are invalid or duplicated");
    }
  }

  CompositionGeometry geometry{
      .bounds =
          {
              .min = {std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::infinity()},
              .max = {-std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity()},
          },
  };
  for (const ParallaxElement& element : layer.elements) {
    const auto size = sizes.find(element.id);
    if (size == sizes.end()) {
      return absl::FailedPreconditionError(
          absl::StrCat("missing dimensions for parallax element ", element.id));
    }
    if (!IsValidElementGeometry(element)) {
      return absl::InvalidArgumentError("parallax element geometry is invalid");
    }
    const WorldRect bounds{
        .min = element.position,
        .max = {element.position.x + size->second.width * static_cast<double>(element.scale),
                element.position.y + size->second.height * static_cast<double>(element.scale)},
    };
    if (!geometry.element_bounds.emplace(element.id, bounds).second) {
      return absl::InvalidArgumentError("parallax element IDs must be unique");
    }
    geometry.bounds.min.x = std::min(geometry.bounds.min.x, bounds.min.x);
    geometry.bounds.min.y = std::min(geometry.bounds.min.y, bounds.min.y);
    geometry.bounds.max.x = std::max(geometry.bounds.max.x, bounds.max.x);
    geometry.bounds.max.y = std::max(geometry.bounds.max.y, bounds.max.y);
  }
  if (sizes.size() != layer.elements.size()) {
    return absl::InvalidArgumentError("parallax element sizes contain an unknown element ID");
  }
  return geometry;
}

}  // namespace

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

std::optional<ActiveParallaxZone> ResolveActiveParallaxZone(const std::vector<ParallaxZone>& zones,
                                                            Vec reference_point) {
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
      .position = {(bounds.min.x + bounds.max.x) / 2.0, (bounds.min.y + bounds.max.y) / 2.0},
      .zoom = std::min(zoom_x, zoom_y),
  };
}

std::optional<CameraFrame> CalculateConstrainedCameraFrame(VisibleWorldBounds target_bounds,
                                                           VisibleWorldBounds world_bounds,
                                                           int viewport_width, int viewport_height,
                                                           double padding_fraction) {
  std::optional<CameraFrame> frame =
      CalculateCameraFrame(target_bounds, viewport_width, viewport_height, padding_fraction);
  if (!frame.has_value()) return std::nullopt;

  const double world_width = world_bounds.max.x - world_bounds.min.x;
  const double world_height = world_bounds.max.y - world_bounds.min.y;
  const double horizontal_clearance =
      std::min(frame->position.x - world_bounds.min.x, world_bounds.max.x - frame->position.x);
  const double vertical_clearance =
      std::min(frame->position.y - world_bounds.min.y, world_bounds.max.y - frame->position.y);
  if (world_width <= 0 || world_height <= 0 || horizontal_clearance <= 0 ||
      vertical_clearance <= 0) {
    return std::nullopt;
  }

  const double minimum_horizontal_zoom = viewport_width / (2.0 * horizontal_clearance);
  const double minimum_vertical_zoom = viewport_height / (2.0 * vertical_clearance);
  frame->zoom = std::max({frame->zoom, minimum_horizontal_zoom, minimum_vertical_zoom});
  return frame;
}

absl::StatusOr<WorldRect> CalculateParallaxCompositionBounds(
    const ParallaxLayer& layer, const std::vector<ParallaxElementSize>& element_sizes) {
  ASSIGN_OR_RETURN(const CompositionGeometry geometry,
                   BuildCompositionGeometry(layer, element_sizes));
  return geometry.bounds;
}

absl::StatusOr<std::vector<ParallaxElementBounds>> CalculateParallaxElementBounds(
    const ParallaxLayer& layer, const std::vector<ParallaxElementSize>& element_sizes) {
  ASSIGN_OR_RETURN(const CompositionGeometry geometry,
                   BuildCompositionGeometry(layer, element_sizes));
  std::vector<ParallaxElementBounds> result;
  result.reserve(layer.elements.size());
  for (const ParallaxElement& element : layer.elements) {
    result.push_back({.element_id = element.id, .bounds = geometry.element_bounds.at(element.id)});
  }
  return result;
}

absl::StatusOr<ParallaxLayout> CalculateParallaxLayout(
    const Camera& camera, const ParallaxLayer& layer,
    const std::vector<ParallaxElementSize>& element_sizes) {
  constexpr int64_t kMaximumVisibleElements = 4096;
  RETURN_IF_ERROR(ValidateLayoutInputs(camera, layer));

  ASSIGN_OR_RETURN(const CompositionGeometry geometry,
                   BuildCompositionGeometry(layer, element_sizes));

  ParallaxLayout layout;
  layout.origin = camera.ParallaxWorldOrigin(layer.scroll_factor, layer.offset);
  const VisibleWorldBounds visible = CalculateVisibleWorldBounds(camera);
  auto copy_range = [](double visible_min, double visible_max, double origin,
                       double content_minimum, double content_maximum,
                       double period) -> absl::StatusOr<CopyRange> {
    if (period == 0.0) {
      if (origin + content_maximum <= visible_min || origin + content_minimum >= visible_max) {
        return std::pair{1, 0};
      }
      return std::pair{0, 0};
    }
    const double first = std::floor((visible_min - origin - content_maximum) / period) + 1.0;
    const double last = std::ceil((visible_max - origin - content_minimum) / period) - 1.0;
    if (!std::isfinite(first) || !std::isfinite(last) || first < std::numeric_limits<int>::min() ||
        first > std::numeric_limits<int>::max() || last < std::numeric_limits<int>::min() ||
        last > std::numeric_limits<int>::max()) {
      return absl::InvalidArgumentError("parallax repeat coordinates exceed supported range");
    }
    return std::pair{static_cast<int>(first), static_cast<int>(last)};
  };

  ASSIGN_OR_RETURN(const CopyRange columns,
                   copy_range(visible.min.x, visible.max.x, layout.origin.x, geometry.bounds.min.x,
                              geometry.bounds.max.x, layer.repeat_period.x));
  ASSIGN_OR_RETURN(const CopyRange rows,
                   copy_range(visible.min.y, visible.max.y, layout.origin.y, geometry.bounds.min.y,
                              geometry.bounds.max.y, layer.repeat_period.y));
  if (columns.second < columns.first || rows.second < rows.first) return layout;

  const int64_t column_count = static_cast<int64_t>(columns.second) - columns.first + 1;
  const int64_t row_count = static_cast<int64_t>(rows.second) - rows.first + 1;
  if (column_count > kMaximumVisibleElements || row_count > kMaximumVisibleElements ||
      column_count * row_count > kMaximumVisibleElements / layer.elements.size()) {
    return absl::ResourceExhaustedError("parallax repeat period produces too many instances");
  }
  layout.elements.reserve(static_cast<size_t>(column_count * row_count) * layer.elements.size());
  for (int64_t row = rows.first; row <= rows.second; ++row) {
    for (int64_t column = columns.first; column <= columns.second; ++column) {
      const Vec repeat_offset{column * layer.repeat_period.x, row * layer.repeat_period.y};
      for (const ParallaxElement& element : layer.elements) {
        const WorldRect local = geometry.element_bounds.at(element.id);
        const WorldRect bounds{
            .min = {layout.origin.x + repeat_offset.x + local.min.x,
                    layout.origin.y + repeat_offset.y + local.min.y},
            .max = {layout.origin.x + repeat_offset.x + local.max.x,
                    layout.origin.y + repeat_offset.y + local.max.y},
        };
        if (!Intersects(bounds, visible)) continue;
        layout.elements.push_back({
            .element_id = element.id,
            .repeat_column = static_cast<int>(column),
            .repeat_row = static_cast<int>(row),
            .bounds = bounds,
        });
      }
    }
  }
  return layout;
}

}  // namespace zebes
