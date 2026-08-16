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
// NAMING. Two words locate the wedge:
//
//   Floor / Ceiling      which tile edge the solid mass hugs. Floor is ground
//                        you walk on; Ceiling is overhead.
//   TallLeft / TallRight the side where the solid reaches full tile height. The
//                        wedge tapers toward the other side, so
//                        kSlope45FloorTallRight is the "/|" triangle: thin at
//                        the left, full height at the right.
//
// Ceiling shapes are exact vertical mirrors of their floor counterparts and
// keep the same direction word, because mirroring a shape vertically does not
// move which side is tall. For the two-tile steep families the mirror also
// swaps the halves, since flipping a 1x2 unit turns its lower tile into its
// upper one: kSteepSlopeCeilingTallRightBottom mirrors
// kSteepSlopeFloorTallRightTop, not ...Bottom.
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
    case TileShape::kSlope45FloorTallRight: {
      static constexpr TilePoint kPoly[] = {{0, 1}, {1, 0}, {1, 1}};
      return kPoly;
    }
    case TileShape::kSlope45FloorTallLeft: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kSlope45CeilingTallRight: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {1, 1}};
      return kPoly;
    }
    case TileShape::kSlope45CeilingTallLeft: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {0, 1}};
      return kPoly;
    }

    // --- Gentle slopes (2:1). Lower spans heights 0 to 1/2, Upper 1/2 to 1. ---
    case TileShape::kGentleSlopeFloorTallRightLower: {
      static constexpr TilePoint kPoly[] = {{0, 1}, {1, .5f}, {1, 1}};
      return kPoly;
    }
    case TileShape::kGentleSlopeFloorTallRightUpper: {
      static constexpr TilePoint kPoly[] = {{0, .5f}, {1, 0}, {1, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kGentleSlopeFloorTallLeftLower: {
      static constexpr TilePoint kPoly[] = {{0, .5f}, {1, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kGentleSlopeFloorTallLeftUpper: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, .5f}, {1, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kGentleSlopeCeilingTallRightLower: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {1, .5f}};
      return kPoly;
    }
    case TileShape::kGentleSlopeCeilingTallRightUpper: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {1, 1}, {0, .5f}};
      return kPoly;
    }
    case TileShape::kGentleSlopeCeilingTallLeftLower: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {0, .5f}};
      return kPoly;
    }
    case TileShape::kGentleSlopeCeilingTallLeftUpper: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {1, .5f}, {0, 1}};
      return kPoly;
    }

    // --- Steep slopes (1:2). Bottom rests on the ground, Top stacks above. ---
    case TileShape::kSteepSlopeFloorTallRightBottom: {
      static constexpr TilePoint kPoly[] = {{0, 1}, {.5f, 0}, {1, 0}, {1, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeFloorTallRightTop: {
      static constexpr TilePoint kPoly[] = {{.5f, 1}, {1, 0}, {1, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeFloorTallLeftBottom: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {.5f, 0}, {1, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeFloorTallLeftTop: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {.5f, 1}, {0, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeCeilingTallRightBottom: {
      static constexpr TilePoint kPoly[] = {{.5f, 0}, {1, 0}, {1, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeCeilingTallRightTop: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {1, 1}, {.5f, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeCeilingTallLeftBottom: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {.5f, 0}, {0, 1}};
      return kPoly;
    }
    case TileShape::kSteepSlopeCeilingTallLeftTop: {
      static constexpr TilePoint kPoly[] = {{0, 0}, {1, 0}, {.5f, 1}, {0, 1}};
      return kPoly;
    }
  }
  return {};
}

}  // namespace zebes
