#pragma once

#include <optional>

#include "absl/status/status.h"
#include "editor/anchor_gizmo.h"
#include "editor/canvas/canvas.h"
#include "imgui.h"

namespace zebes {

struct AnchorGizmoStyle {
  ImU32 color = IM_COL32(255, 96, 96, 255);
  float thickness = 2.0f;
};

absl::Status DrawAnchorGizmo(ImDrawList& draw_list, Vec screen_origin,
                             std::optional<BlueprintPlacementMode> placement_mode,
                             const AnchorGizmoStyle& style = {});

absl::Status DrawWorldAnchorGizmo(ImDrawList& draw_list, const Canvas& canvas, Vec world_origin,
                                  std::optional<BlueprintPlacementMode> placement_mode,
                                  const AnchorGizmoStyle& style = {});

}  // namespace zebes
