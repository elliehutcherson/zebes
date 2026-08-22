#include "editor/anchor_gizmo.h"

#include <cmath>

#include "absl/status/status.h"

namespace zebes {

absl::StatusOr<AnchorGizmoGeometry> CalculateAnchorGizmoGeometry(
    Vec screen_origin, std::optional<BlueprintPlacementMode> placement_mode,
    const AnchorGizmoMetrics& metrics) {
  if (!std::isfinite(screen_origin.x) || !std::isfinite(screen_origin.y)) {
    return absl::InvalidArgumentError("anchor gizmo origin must be finite");
  }
  if (!std::isfinite(metrics.origin_arm_length) || !std::isfinite(metrics.surface_half_width) ||
      !std::isfinite(metrics.surface_tick_length) || metrics.origin_arm_length <= 0.0 ||
      metrics.surface_half_width <= metrics.origin_arm_length ||
      metrics.surface_tick_length <= 0.0) {
    return absl::InvalidArgumentError("anchor gizmo metrics are invalid");
  }
  if (placement_mode.has_value() && !IsValidBlueprintPlacementMode(*placement_mode)) {
    return absl::InvalidArgumentError("anchor gizmo placement mode is invalid");
  }

  AnchorGizmoGeometry geometry;
  geometry.origin = {
      {.start = {screen_origin.x - metrics.origin_arm_length, screen_origin.y},
       .end = {screen_origin.x + metrics.origin_arm_length, screen_origin.y}},
      {.start = {screen_origin.x, screen_origin.y - metrics.origin_arm_length},
       .end = {screen_origin.x, screen_origin.y + metrics.origin_arm_length}},
  };

  if (!placement_mode.has_value() || *placement_mode == BlueprintPlacementMode::kFree) {
    return geometry;
  }

  // Screen Y grows downward. Grounded artwork occupies the space above its
  // surface, while ceiling artwork occupies the space below it.
  const double tick_direction = *placement_mode == BlueprintPlacementMode::kGrounded ? -1.0 : 1.0;
  const double left = screen_origin.x - metrics.surface_half_width;
  const double right = screen_origin.x + metrics.surface_half_width;
  const double inner_left = screen_origin.x - metrics.origin_arm_length;
  const double inner_right = screen_origin.x + metrics.origin_arm_length;
  const double tick_end_y = screen_origin.y + tick_direction * metrics.surface_tick_length;
  geometry.surface = {
      {.start = {left, screen_origin.y}, .end = {inner_left, screen_origin.y}},
      {.start = {inner_right, screen_origin.y}, .end = {right, screen_origin.y}},
      {.start = {left, screen_origin.y}, .end = {left, tick_end_y}},
      {.start = {right, screen_origin.y}, .end = {right, tick_end_y}},
  };
  return geometry;
}

}  // namespace zebes
