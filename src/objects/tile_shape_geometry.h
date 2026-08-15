#pragma once

#include "absl/types/span.h"
#include "objects/tileset.h"

namespace zebes {

// A point in normalized tile space: (0,0) is the tile's top-left corner, (1,1)
// its bottom-right. y points DOWN, matching how source_x/source_y index an
// atlas and how the editor lays out screen space.
struct TilePoint {
  float x = 0.0f;
  float y = 0.0f;
};

// The solid region of a tile shape, as a convex polygon wound clockwise.
//
// This is the only definition of what a TileShape means geometrically. Tiles
// render as plain textured quads and nothing derives geometry from the shape at
// draw time, so a tile's artwork *is* its solid volume: the tool that draws
// terrain and the overlay that visualizes it must read their polygon from here,
// or they will disagree with no test able to notice.
//
// NAMING. The enumerator names describe the wedge, not its right angle:
//
//   Bottom / Top   which tile edge the solid mass hugs. Bottom is ground you
//                  walk on; Top is a ceiling.
//   Left / Right   the side the wedge tapers to zero thickness on -- the low
//                  end of the slope surface. The right angle therefore sits at
//                  the opposite horizontal corner, so kSlope45BottomLeft is the
//                  "/|" triangle: thin at the left, right-angled at the
//                  bottom-right.
//
// Ceiling shapes are exact vertical mirrors of their floor counterparts. For
// the two-tile steep families the mirror also swaps the halves, since flipping
// a 1x2 unit turns its lower tile into its upper one: kSteepSlopeTopLeftBottom
// mirrors kSteepSlopeBottomLeftTop, not ..._Bottom.
//
// kNone yields an empty span.
inline absl::Span<const TilePoint> TileShapePolygon(TileShape shape) {
  switch (shape) {
    case TileShape::kNone:
      break;

    case TileShape::kFullBlock: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
      return kPoly;
    }

    // --- Half blocks ---
    case TileShape::kHalfBlockBottom: {
      static constexpr TilePoint kPoly[] = {{0, .5f}, {1, .5f}, {1, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kHalfBlockTop: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {1, .5f}, {0, .5f}};
      return kPoly;
    }
    case TileShape::kHalfBlockLeft: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {.5f, 0}, {.5f, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kHalfBlockRight: {
      static constexpr TilePoint kPoly[] = {{.5f, 0}, {1, 0}, {1, 1}, {.5f, 1}};
      return kPoly;
    }

    // --- 45-degree slopes (1:1) ---
    case TileShape::kSlope45BottomLeft: {
      static constexpr TilePoint kPoly[] = {{0, 1}, {1, 0}, {1, 1}};
      return kPoly;
    }
    case TileShape::kSlope45BottomRight: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kSlope45TopLeft: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {1, 1}};
      return kPoly;
    }
    case TileShape::kSlope45TopRight: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {0, 1}};
      return kPoly;
    }

    // --- Gentle slopes (2:1). Lower spans heights 0 to 1/2, Upper 1/2 to 1. ---
    case TileShape::kGentleSlopeBottomLeftLower: {
      static constexpr TilePoint kPoly[] = {{0, 1}, {1, .5f}, {1, 1}};
      return kPoly;
    }
    case TileShape::kGentleSlopeBottomLeftUpper: {
      static constexpr TilePoint kPoly[] = {{0, .5f}, {1, 0}, {1, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kGentleSlopeBottomRightLower: {
      static constexpr TilePoint kPoly[] = {{0, .5f}, {1, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kGentleSlopeBottomRightUpper: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, .5f}, {1, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kGentleSlopeTopLeftLower: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {1, .5f}};
      return kPoly;
    }
    case TileShape::kGentleSlopeTopLeftUpper: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {1, 1}, {0, .5f}};
      return kPoly;
    }
    case TileShape::kGentleSlopeTopRightLower: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {0, .5f}};
      return kPoly;
    }
    case TileShape::kGentleSlopeTopRightUpper: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {1, .5f}, {0, 1}};
      return kPoly;
    }

    // --- Steep slopes (1:2). Bottom rests on the ground, Top stacks above. ---
    case TileShape::kSteepSlopeBottomLeftBottom: {
      static constexpr TilePoint kPoly[] = {{0, 1}, {.5f, 0}, {1, 0}, {1, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeBottomLeftTop: {
      static constexpr TilePoint kPoly[] = {{.5f, 1}, {1, 0}, {1, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeBottomRightBottom: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {.5f, 0}, {1, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeBottomRightTop: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {.5f, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeTopLeftBottom: {
      static constexpr TilePoint kPoly[] = {{.5f, 0}, {1, 0}, {1, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeTopLeftTop: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {1, 1}, {.5f, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeTopRightBottom: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {.5f, 0}, {0, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeTopRightTop: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {.5f, 1}, {0, 1}};
      return kPoly;
    }
  }
  return {};
}

}  // namespace zebes
