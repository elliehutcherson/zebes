#pragma once

#include <initializer_list>
#include <vector>

#include "imgui.h"
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
// Polygon vertices are in normalized [0,1] tile space: (0,0)=top-left,
// (1,1)=bottom-right. Slope geometries follow the naming convention in
// tileset.h. Gentle and steep slope variants approximate the 2:1 / 1:2
// geometry; validate against the physics collision system if precise accuracy
// is needed.
inline void DrawShapeOverlay(ImDrawList* dl, const ImVec2& cell_min,
                             const ImVec2& cell_max, TileShape shape) {
  constexpr ImU32 kFillColor = IM_COL32(255, 80, 0, 60);
  constexpr ImU32 kLineColor = IM_COL32(255, 80, 0, 220);
  constexpr float kLineWidth = 2.0f;

  auto draw = [&](std::initializer_list<ImVec2> pts) {
    std::vector<ImVec2> screen;
    screen.reserve(pts.size());
    for (const ImVec2& p : pts) screen.push_back(TileToScreen(p, cell_min, cell_max));
    dl->AddConvexPolyFilled(screen.data(), static_cast<int>(screen.size()), kFillColor);
    dl->AddPolyline(screen.data(), static_cast<int>(screen.size()), kLineColor,
                    ImDrawFlags_Closed, kLineWidth);
  };

  switch (shape) {
    case TileShape::kNone:
      break;

    case TileShape::kFullBlock:
      draw({{0, 0}, {1, 0}, {1, 1}, {0, 1}});
      break;

    // --- Half Blocks ---
    case TileShape::kHalfBlockBottom:
      draw({{0, .5f}, {1, .5f}, {1, 1}, {0, 1}});
      break;
    case TileShape::kHalfBlockTop:
      draw({{0, 0}, {1, 0}, {1, .5f}, {0, .5f}});
      break;
    case TileShape::kHalfBlockLeft:
      draw({{0, 0}, {.5f, 0}, {.5f, 1}, {0, 1}});
      break;
    case TileShape::kHalfBlockRight:
      draw({{.5f, 0}, {1, 0}, {1, 1}, {.5f, 1}});
      break;

    // --- 45-Degree Slopes ---
    case TileShape::kSlope45BottomLeft:
      draw({{0, 1}, {1, 0}, {1, 1}});
      break;
    case TileShape::kSlope45BottomRight:
      draw({{0, 0}, {0, 1}, {1, 1}});
      break;
    case TileShape::kSlope45TopLeft:
      draw({{0, 0}, {1, 0}, {1, 1}});
      break;
    case TileShape::kSlope45TopRight:
      draw({{0, 0}, {1, 0}, {0, 1}});
      break;

    // --- Gentle Slopes (2:1 ratio, ~26.5 degrees) ---
    case TileShape::kGentleSlopeBottomLeft_Lower:
      draw({{0, 1}, {1, .5f}, {1, 1}});
      break;
    case TileShape::kGentleSlopeBottomLeft_Upper:
      draw({{0, .5f}, {1, 0}, {1, 1}, {0, 1}});
      break;
    case TileShape::kGentleSlopeBottomRight_Lower:
      draw({{0, .5f}, {1, 1}, {0, 1}});
      break;
    case TileShape::kGentleSlopeBottomRight_Upper:
      draw({{0, 0}, {1, .5f}, {1, 1}, {0, 1}});
      break;
    case TileShape::kGentleSlopeTopLeft_Lower:
      draw({{0, 0}, {1, 0}, {1, .5f}});
      break;
    case TileShape::kGentleSlopeTopLeft_Upper:
      draw({{0, 0}, {1, 0}, {1, 1}, {0, .5f}});
      break;
    case TileShape::kGentleSlopeTopRight_Lower:
      draw({{0, 0}, {1, 0}, {0, .5f}});
      break;
    case TileShape::kGentleSlopeTopRight_Upper:
      draw({{0, 0}, {1, 0}, {1, .5f}, {0, 1}});
      break;

    // --- Steep Slopes (1:2 ratio, ~63.4 degrees) ---
    case TileShape::kSteepSlopeBottomLeft_Bottom:
      draw({{0, 1}, {.5f, 0}, {1, 0}, {1, 1}});
      break;
    case TileShape::kSteepSlopeBottomLeft_Top:
      draw({{0, 0}, {0, 1}, {.5f, 1}, {1, 0}});
      break;
    case TileShape::kSteepSlopeBottomRight_Bottom:
      draw({{0, 0}, {.5f, 0}, {1, 1}, {0, 1}});
      break;
    case TileShape::kSteepSlopeBottomRight_Top:
      draw({{0, 0}, {1, 0}, {1, 1}, {.5f, 1}});
      break;
    case TileShape::kSteepSlopeTopLeft_Bottom:
      draw({{0, 0}, {.5f, 0}, {1, 1}, {0, 1}});
      break;
    case TileShape::kSteepSlopeTopLeft_Top:
      draw({{0, 0}, {1, 0}, {.5f, 1}, {0, 1}});
      break;
    case TileShape::kSteepSlopeTopRight_Bottom:
      draw({{0, 1}, {.5f, 0}, {1, 0}, {1, 1}});
      break;
    case TileShape::kSteepSlopeTopRight_Top:
      draw({{0, 0}, {1, 0}, {1, 1}, {.5f, 1}});
      break;
  }
}

}  // namespace zebes
