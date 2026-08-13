#pragma once

#include <vector>

#include "imgui.h"
#include "objects/tile_shape_geometry.h"
#include "objects/tileset.h"

namespace zebes {

// Converts a normalized [0,1] point within a tile cell to ImGui screen space.
inline ImVec2 TileToScreen(const ImVec2& norm, const ImVec2& cell_min,
                            const ImVec2& cell_max) {
  return {cell_min.x + norm.x * (cell_max.x - cell_min.x),
          cell_min.y + norm.y * (cell_max.y - cell_min.y)};
}

// Draws the collision-shape overlay for a tile on the given draw list.
//
// The geometry itself lives in objects/tile_shape_geometry.h, shared with the
// terrain generator so drawn artwork and this overlay cannot drift apart.
inline void DrawShapeOverlay(ImDrawList* dl, const ImVec2& cell_min,
                             const ImVec2& cell_max, TileShape shape) {
  constexpr ImU32 kFillColor = IM_COL32(255, 80, 0, 60);
  constexpr ImU32 kLineColor = IM_COL32(255, 80, 0, 220);
  constexpr float kLineWidth = 2.0f;

  const absl::Span<const TilePoint> polygon = TileShapePolygon(shape);
  if (polygon.empty()) return;

  std::vector<ImVec2> screen;
  screen.reserve(polygon.size());
  for (const TilePoint& point : polygon) {
    screen.push_back(TileToScreen(ImVec2(point.x, point.y), cell_min, cell_max));
  }
  dl->AddConvexPolyFilled(screen.data(), static_cast<int>(screen.size()), kFillColor);
  dl->AddPolyline(screen.data(), static_cast<int>(screen.size()), kLineColor,
                  ImDrawFlags_Closed, kLineWidth);
}

}  // namespace zebes
