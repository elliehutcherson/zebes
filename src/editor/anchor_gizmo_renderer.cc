#include "editor/anchor_gizmo_renderer.h"

#include <cmath>

#include "absl/status/status.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

void DrawLines(ImDrawList& draw_list, const std::vector<AnchorGizmoLine>& lines,
               const AnchorGizmoStyle& style) {
  for (const AnchorGizmoLine& line : lines) {
    draw_list.AddLine(ImVec2(static_cast<float>(line.start.x), static_cast<float>(line.start.y)),
                      ImVec2(static_cast<float>(line.end.x), static_cast<float>(line.end.y)),
                      style.color, style.thickness);
  }
}

}  // namespace

absl::Status DrawAnchorGizmo(ImDrawList& draw_list, Vec screen_origin,
                             std::optional<BlueprintPlacementMode> placement_mode,
                             const AnchorGizmoStyle& style) {
  if (!std::isfinite(style.thickness) || style.thickness <= 0.0f) {
    return absl::InvalidArgumentError("anchor gizmo line thickness must be positive");
  }
  ASSIGN_OR_RETURN(const AnchorGizmoGeometry geometry,
                   CalculateAnchorGizmoGeometry(screen_origin, placement_mode));
  DrawLines(draw_list, geometry.surface, style);
  DrawLines(draw_list, geometry.origin, style);
  return absl::OkStatus();
}

absl::Status DrawWorldAnchorGizmo(ImDrawList& draw_list, const Canvas& canvas, Vec world_origin,
                                  std::optional<BlueprintPlacementMode> placement_mode,
                                  const AnchorGizmoStyle& style) {
  const ImVec2 screen = canvas.WorldToScreen(world_origin);
  return DrawAnchorGizmo(draw_list, {screen.x, screen.y}, placement_mode, style);
}

}  // namespace zebes
