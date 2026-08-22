#pragma once

#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "objects/blueprint.h"
#include "objects/vec.h"

namespace zebes {

struct AnchorGizmoLine {
  Vec start;
  Vec end;

  bool operator==(const AnchorGizmoLine& other) const = default;
};

// Screen-space geometry for an authored entity origin. Grounded and ceiling
// modes add a surface bracket pointing toward the side occupied by artwork;
// an absent mode draws only the origin cross for unresolved legacy entities.
struct AnchorGizmoGeometry {
  std::vector<AnchorGizmoLine> origin;
  std::vector<AnchorGizmoLine> surface;
};

struct AnchorGizmoMetrics {
  double origin_arm_length = 6.0;
  double surface_half_width = 13.0;
  double surface_tick_length = 4.0;
};

// Calculates backend-neutral screen-space lines centered on screen_origin.
// The returned geometry has constant visual size regardless of canvas zoom.
absl::StatusOr<AnchorGizmoGeometry> CalculateAnchorGizmoGeometry(
    Vec screen_origin, std::optional<BlueprintPlacementMode> placement_mode,
    const AnchorGizmoMetrics& metrics = {});

}  // namespace zebes
