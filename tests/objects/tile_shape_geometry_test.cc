// Pins the polygon of every TileShape.
//
// Nothing derives geometry from a tile's shape at render time -- tiles are
// drawn as plain textured quads -- so these polygons are what the artwork has
// to agree with, and until this file existed no test asserted a single vertex.
// That is how six of the eight steep-slope polygons came to be wrong: two were
// the exact complement of the solid region, and three were byte-identical
// copies of floor shapes.

#include "objects/tile_shape_geometry.h"

#include <algorithm>
#include <vector>

#include "gtest/gtest.h"
#include "objects/tileset.h"

namespace zebes {
namespace {

using Points = std::vector<TilePoint>;

Points PolygonOf(TileShape shape) {
  const absl::Span<const TilePoint> polygon = TileShapePolygon(shape);
  return Points(polygon.begin(), polygon.end());
}

// Compares polygons as point sets, so a test can state which corners a shape
// occupies without also pinning which vertex the winding happens to start at.
Points Sorted(Points points) {
  std::sort(points.begin(), points.end(), [](const TilePoint& a, const TilePoint& b) {
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
  });
  return points;
}

bool SamePoints(const Points& a, const Points& b) {
  const Points sorted_a = Sorted(a);
  const Points sorted_b = Sorted(b);
  if (sorted_a.size() != sorted_b.size()) return false;
  for (size_t i = 0; i < sorted_a.size(); ++i) {
    if (sorted_a[i].x != sorted_b[i].x || sorted_a[i].y != sorted_b[i].y) return false;
  }
  return true;
}

Points MirrorY(const Points& points) {
  Points mirrored;
  mirrored.reserve(points.size());
  for (const TilePoint& point : points) mirrored.push_back(TilePoint{point.x, 1.0f - point.y});
  return mirrored;
}

Points MirrorX(const Points& points) {
  Points mirrored;
  mirrored.reserve(points.size());
  for (const TilePoint& point : points) mirrored.push_back(TilePoint{1.0f - point.x, point.y});
  return mirrored;
}

// The highest solid point on one vertical edge of a tile, as a y coordinate.
// Slope units meet when the surface arrives at the shared edge at one height
// and leaves it at the same one.
float TopmostYAtX(const Points& points, float x) {
  float top = 2.0f;
  for (const TilePoint& point : points) {
    if (point.x == x) top = std::min(top, point.y);
  }
  return top;
}

std::vector<TileShape> AllShapes() {
  std::vector<TileShape> shapes;
  for (int value = 0; value <= static_cast<int>(TileShape::kSteepSlopeCeilingTallLeftTop);
       ++value) {
    shapes.push_back(static_cast<TileShape>(value));
  }
  return shapes;
}

TEST(TileShapeGeometryTest, EveryShapeHasTheExpectedPolygon) {
  const std::vector<std::pair<TileShape, Points>> expected = {
      {TileShape::kFullBlock, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},

      {TileShape::kHalfBlockBottom, {{0, .5f}, {1, .5f}, {1, 1}, {0, 1}}},
      {TileShape::kHalfBlockTop, {{0, 0}, {1, 0}, {1, .5f}, {0, .5f}}},
      {TileShape::kHalfBlockLeft, {{0, 0}, {.5f, 0}, {.5f, 1}, {0, 1}}},
      {TileShape::kHalfBlockRight, {{.5f, 0}, {1, 0}, {1, 1}, {.5f, 1}}},

      {TileShape::kSlope45FloorTallRight, {{0, 1}, {1, 0}, {1, 1}}},
      {TileShape::kSlope45FloorTallLeft, {{0, 0}, {0, 1}, {1, 1}}},
      {TileShape::kSlope45CeilingTallRight, {{0, 0}, {1, 0}, {1, 1}}},
      {TileShape::kSlope45CeilingTallLeft, {{0, 0}, {1, 0}, {0, 1}}},

      {TileShape::kGentleSlopeFloorTallRightLower, {{0, 1}, {1, .5f}, {1, 1}}},
      {TileShape::kGentleSlopeFloorTallRightUpper, {{0, .5f}, {1, 0}, {1, 1}, {0, 1}}},
      {TileShape::kGentleSlopeFloorTallLeftLower, {{0, .5f}, {1, 1}, {0, 1}}},
      {TileShape::kGentleSlopeFloorTallLeftUpper, {{0, 0}, {1, .5f}, {1, 1}, {0, 1}}},
      {TileShape::kGentleSlopeCeilingTallRightLower, {{0, 0}, {1, 0}, {1, .5f}}},
      {TileShape::kGentleSlopeCeilingTallRightUpper, {{0, 0}, {1, 0}, {1, 1}, {0, .5f}}},
      {TileShape::kGentleSlopeCeilingTallLeftLower, {{0, 0}, {1, 0}, {0, .5f}}},
      {TileShape::kGentleSlopeCeilingTallLeftUpper, {{0, 0}, {1, 0}, {1, .5f}, {0, 1}}},

      {TileShape::kSteepSlopeFloorTallRightBottom, {{0, 1}, {.5f, 0}, {1, 0}, {1, 1}}},
      {TileShape::kSteepSlopeFloorTallRightTop, {{.5f, 1}, {1, 0}, {1, 1}}},
      {TileShape::kSteepSlopeFloorTallLeftBottom, {{0, 0}, {.5f, 0}, {1, 1}, {0, 1}}},
      {TileShape::kSteepSlopeFloorTallLeftTop, {{0, 0}, {.5f, 1}, {0, 1}}},
      {TileShape::kSteepSlopeCeilingTallRightBottom, {{.5f, 0}, {1, 0}, {1, 1}}},
      {TileShape::kSteepSlopeCeilingTallRightTop, {{0, 0}, {1, 0}, {1, 1}, {.5f, 1}}},
      {TileShape::kSteepSlopeCeilingTallLeftBottom, {{0, 0}, {.5f, 0}, {0, 1}}},
      {TileShape::kSteepSlopeCeilingTallLeftTop, {{0, 0}, {1, 0}, {.5f, 1}, {0, 1}}},
  };

  for (const auto& [shape, points] : expected) {
    EXPECT_TRUE(SamePoints(PolygonOf(shape), points))
        << "shape " << kTileShapeIdentifiers[static_cast<int>(shape)] << " has the wrong polygon";
  }
}

TEST(TileShapeGeometryTest, NoShapeIsEmptyExceptNone) {
  EXPECT_TRUE(TileShapePolygon(TileShape::kNone).empty());
  for (const TileShape shape : AllShapes()) {
    if (shape == TileShape::kNone) continue;
    EXPECT_GE(TileShapePolygon(shape).size(), 3u)
        << kTileShapeIdentifiers[static_cast<int>(shape)] << " is not a polygon";
  }
}

// Two shapes sharing a polygon means one of them was never derived. Three such
// duplicates were how the wrong steep ceiling shapes hid.
TEST(TileShapeGeometryTest, NoTwoShapesShareAPolygon) {
  const std::vector<TileShape> shapes = AllShapes();
  for (size_t i = 1; i < shapes.size(); ++i) {
    for (size_t j = i + 1; j < shapes.size(); ++j) {
      EXPECT_FALSE(SamePoints(PolygonOf(shapes[i]), PolygonOf(shapes[j])))
          << kTileShapeIdentifiers[static_cast<int>(shapes[i])] << " and "
          << kTileShapeIdentifiers[static_cast<int>(shapes[j])] << " are the same polygon";
    }
  }
}

// A ceiling shape is its floor counterpart flipped. For the two-tile steep
// families the flip also swaps the halves, because turning a 1x2 unit upside
// down makes its lower tile the upper one.
TEST(TileShapeGeometryTest, CeilingShapesMirrorTheirFloorCounterparts) {
  const std::vector<std::pair<TileShape, TileShape>> mirrored = {
      {TileShape::kHalfBlockBottom, TileShape::kHalfBlockTop},

      {TileShape::kSlope45FloorTallRight, TileShape::kSlope45CeilingTallRight},
      {TileShape::kSlope45FloorTallLeft, TileShape::kSlope45CeilingTallLeft},

      {TileShape::kGentleSlopeFloorTallRightLower, TileShape::kGentleSlopeCeilingTallRightLower},
      {TileShape::kGentleSlopeFloorTallRightUpper, TileShape::kGentleSlopeCeilingTallRightUpper},
      {TileShape::kGentleSlopeFloorTallLeftLower, TileShape::kGentleSlopeCeilingTallLeftLower},
      {TileShape::kGentleSlopeFloorTallLeftUpper, TileShape::kGentleSlopeCeilingTallLeftUpper},

      {TileShape::kSteepSlopeFloorTallRightTop, TileShape::kSteepSlopeCeilingTallRightBottom},
      {TileShape::kSteepSlopeFloorTallRightBottom, TileShape::kSteepSlopeCeilingTallRightTop},
      {TileShape::kSteepSlopeFloorTallLeftTop, TileShape::kSteepSlopeCeilingTallLeftBottom},
      {TileShape::kSteepSlopeFloorTallLeftBottom, TileShape::kSteepSlopeCeilingTallLeftTop},
  };

  for (const auto& [floor, ceiling] : mirrored) {
    EXPECT_TRUE(SamePoints(MirrorY(PolygonOf(floor)), PolygonOf(ceiling)))
        << kTileShapeIdentifiers[static_cast<int>(ceiling)] << " is not the vertical mirror of "
        << kTileShapeIdentifiers[static_cast<int>(floor)];
  }
}

// The left-rising and right-rising families are each other's reflections, which
// is what keeps a ramp symmetric whichever way the player runs up it.
TEST(TileShapeGeometryTest, LeftAndRightFamiliesMirrorHorizontally) {
  const std::vector<std::pair<TileShape, TileShape>> mirrored = {
      {TileShape::kHalfBlockLeft, TileShape::kHalfBlockRight},

      {TileShape::kSlope45FloorTallRight, TileShape::kSlope45FloorTallLeft},
      {TileShape::kSlope45CeilingTallRight, TileShape::kSlope45CeilingTallLeft},

      {TileShape::kGentleSlopeFloorTallRightLower, TileShape::kGentleSlopeFloorTallLeftLower},
      {TileShape::kGentleSlopeFloorTallRightUpper, TileShape::kGentleSlopeFloorTallLeftUpper},

      {TileShape::kSteepSlopeFloorTallRightBottom, TileShape::kSteepSlopeFloorTallLeftBottom},
      {TileShape::kSteepSlopeFloorTallRightTop, TileShape::kSteepSlopeFloorTallLeftTop},
  };

  for (const auto& [left, right] : mirrored) {
    EXPECT_TRUE(SamePoints(MirrorX(PolygonOf(left)), PolygonOf(right)))
        << kTileShapeIdentifiers[static_cast<int>(right)] << " is not the horizontal mirror of "
        << kTileShapeIdentifiers[static_cast<int>(left)];
  }
}

// Every polygon is convex and wound the same way, which AddConvexPolyFilled
// relies on and which a hand-edited vertex list breaks silently.
TEST(TileShapeGeometryTest, EveryPolygonIsConvexAndWoundClockwise) {
  for (const TileShape shape : AllShapes()) {
    const Points points = PolygonOf(shape);
    if (points.size() < 3) continue;

    for (size_t i = 0; i < points.size(); ++i) {
      const TilePoint& a = points[i];
      const TilePoint& b = points[(i + 1) % points.size()];
      const TilePoint& c = points[(i + 2) % points.size()];
      const float cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
      EXPECT_GT(cross, 0.0f) << kTileShapeIdentifiers[static_cast<int>(shape)]
                             << " turns the wrong way at vertex " << i;
    }
  }
}

TEST(TileShapeGeometryTest, EveryVertexIsInsideTheTile) {
  for (const TileShape shape : AllShapes()) {
    for (const TilePoint& point : TileShapePolygon(shape)) {
      EXPECT_GE(point.x, 0.0f);
      EXPECT_LE(point.x, 1.0f);
      EXPECT_GE(point.y, 0.0f);
      EXPECT_LE(point.y, 1.0f);
    }
  }
}

// A gentle slope is two tiles. They only read as one ramp if the surface leaves
// one tile at the height it enters the next.
TEST(TileShapeGeometryTest, GentleSlopeHalvesMeetAtHalfHeight) {
  // Rising to the right: Lower is the left tile, Upper the right one.
  EXPECT_EQ(TopmostYAtX(PolygonOf(TileShape::kGentleSlopeFloorTallRightLower), 1.0f), 0.5f);
  EXPECT_EQ(TopmostYAtX(PolygonOf(TileShape::kGentleSlopeFloorTallRightUpper), 0.0f), 0.5f);
  EXPECT_EQ(TopmostYAtX(PolygonOf(TileShape::kGentleSlopeFloorTallRightLower), 0.0f), 1.0f);
  EXPECT_EQ(TopmostYAtX(PolygonOf(TileShape::kGentleSlopeFloorTallRightUpper), 1.0f), 0.0f);

  // Rising to the left: Upper is the left tile, Lower the right one.
  EXPECT_EQ(TopmostYAtX(PolygonOf(TileShape::kGentleSlopeFloorTallLeftUpper), 1.0f), 0.5f);
  EXPECT_EQ(TopmostYAtX(PolygonOf(TileShape::kGentleSlopeFloorTallLeftLower), 0.0f), 0.5f);
  EXPECT_EQ(TopmostYAtX(PolygonOf(TileShape::kGentleSlopeFloorTallLeftLower), 1.0f), 1.0f);
  EXPECT_EQ(TopmostYAtX(PolygonOf(TileShape::kGentleSlopeFloorTallLeftUpper), 0.0f), 0.0f);
}

// A steep slope is two stacked tiles, so they meet along a horizontal edge: the
// upper tile's solid width at its bottom edge is the lower tile's at its top.
TEST(TileShapeGeometryTest, SteepSlopeHalvesMeetAtHalfWidth) {
  // Rising to the right: the surface crosses x = 0.5 exactly at the seam.
  EXPECT_EQ(TopmostYAtX(PolygonOf(TileShape::kSteepSlopeFloorTallRightBottom), 0.5f), 0.0f);
  EXPECT_EQ(TopmostYAtX(PolygonOf(TileShape::kSteepSlopeFloorTallRightTop), 0.5f), 1.0f);
  EXPECT_EQ(TopmostYAtX(PolygonOf(TileShape::kSteepSlopeFloorTallRightBottom), 0.0f), 1.0f);
  EXPECT_EQ(TopmostYAtX(PolygonOf(TileShape::kSteepSlopeFloorTallRightTop), 1.0f), 0.0f);
}

}  // namespace
}  // namespace zebes
