#include "terrain/terrain_mask.h"

#include <array>
#include <set>

#include "gtest/gtest.h"

namespace zebes {
namespace {

TEST(TerrainMaskTest, NormalizeClearsCornersWithoutBothFlankingEdges) {
  // A north-east diagonal alone is invisible: the cell does not reach it.
  EXPECT_EQ(NormalizeNeighborMask(kNorthEast), 0);
  // One flanking edge is still not enough.
  EXPECT_EQ(NormalizeNeighborMask(kNorthEast | kNorth), kNorth);
  EXPECT_EQ(NormalizeNeighborMask(kNorthEast | kEast), kEast);
  // Both flanking edges keep the diagonal.
  EXPECT_EQ(NormalizeNeighborMask(kNorthEast | kNorth | kEast), kNorthEast | kNorth | kEast);
}

TEST(TerrainMaskTest, NormalizeHandlesEveryCornerIndependently) {
  const uint8_t all_diagonals = kNorthEast | kSouthEast | kSouthWest | kNorthWest;
  EXPECT_EQ(NormalizeNeighborMask(all_diagonals), 0);

  // South and west present, so only the south-west diagonal survives.
  const uint8_t south_west_only = all_diagonals | kSouth | kWest;
  EXPECT_EQ(NormalizeNeighborMask(south_west_only), kSouthWest | kSouth | kWest);
}

TEST(TerrainMaskTest, NormalizeIsIdempotent) {
  for (int raw = 0; raw < 256; ++raw) {
    const uint8_t once = NormalizeNeighborMask(static_cast<uint8_t>(raw));
    EXPECT_EQ(NormalizeNeighborMask(once), once) << "raw mask " << raw;
  }
}

TEST(TerrainMaskTest, TableHoldsExactlyFortySevenUniqueMasks) {
  absl::Span<const uint8_t> table = Blob47MaskTable();
  ASSERT_EQ(table.size(), kBlob47TileCount);

  std::set<uint8_t> unique(table.begin(), table.end());
  EXPECT_EQ(unique.size(), kBlob47TileCount);
}

TEST(TerrainMaskTest, TableIsAscending) {
  absl::Span<const uint8_t> table = Blob47MaskTable();
  for (size_t i = 1; i < table.size(); ++i) {
    EXPECT_LT(table[i - 1], table[i]) << "at index " << i;
  }
}

TEST(TerrainMaskTest, EveryRawMaskNormalizesIntoTheTable) {
  std::set<uint8_t> table(Blob47MaskTable().begin(), Blob47MaskTable().end());
  for (int raw = 0; raw < 256; ++raw) {
    const uint8_t normalized = NormalizeNeighborMask(static_cast<uint8_t>(raw));
    EXPECT_EQ(table.count(normalized), 1u) << "raw mask " << raw << " normalized to "
                                           << static_cast<int>(normalized);
  }
}

TEST(TerrainMaskTest, IndexRoundTripsWithTable) {
  absl::Span<const uint8_t> table = Blob47MaskTable();
  for (int i = 0; i < static_cast<int>(table.size()); ++i) {
    std::optional<int> index = Blob47IndexForMask(table[i]);
    ASSERT_TRUE(index.has_value()) << "mask " << static_cast<int>(table[i]);
    EXPECT_EQ(*index, i);
  }
}

TEST(TerrainMaskTest, IndexRejectsNonNormalizedMasks) {
  // A lone diagonal never appears in the table.
  EXPECT_FALSE(Blob47IndexForMask(kNorthEast).has_value());
  EXPECT_FALSE(Blob47IndexForMask(kSouthWest | kSouth).has_value());
}

// Pins the ordering that the compositor, the tileset importer, and the brush all
// index artwork by. A change here silently repaints every existing level, so the
// table is asserted literally rather than recomputed.
TEST(TerrainMaskTest, TableOrderingMatchesGolden) {
  const std::array<uint8_t, kBlob47TileCount> kGolden = {
      0,   1,   4,   5,   7,   16,  17,  20,  21,  23,  28,  29,  31,  64,  65,  68,
      69,  71,  80,  81,  84,  85,  87,  92,  93,  95,  112, 113, 116, 117, 119, 124,
      125, 127, 193, 197, 199, 209, 213, 215, 221, 223, 241, 245, 247, 253, 255,
  };

  absl::Span<const uint8_t> table = Blob47MaskTable();
  ASSERT_EQ(table.size(), kGolden.size());
  for (size_t i = 0; i < kGolden.size(); ++i) {
    EXPECT_EQ(table[i], kGolden[i]) << "at index " << i;
  }
}

TEST(TerrainMaskTest, QuadrantStateCoversEveryTableEntry) {
  const std::array<Quadrant, kQuadrantCount> kQuadrants = {
      Quadrant::kNorthWest,
      Quadrant::kNorthEast,
      Quadrant::kSouthEast,
      Quadrant::kSouthWest,
  };

  for (uint8_t mask : Blob47MaskTable()) {
    for (Quadrant quadrant : kQuadrants) {
      const QuadrantState state = QuadrantStateForMask(mask, quadrant);
      EXPECT_LE(static_cast<int>(state), kQuadrantStateCount - 1)
          << "mask " << static_cast<int>(mask);
    }
  }
}

TEST(TerrainMaskTest, QuadrantStateDistinguishesInnerFromOuterCorner) {
  // Fully surrounded: every quadrant is interior fill.
  for (int q = 0; q < kQuadrantCount; ++q) {
    EXPECT_EQ(QuadrantStateForMask(255, static_cast<Quadrant>(q)), QuadrantState::kFill);
  }

  // Isolated cell: every quadrant is an outward convex corner.
  for (int q = 0; q < kQuadrantCount; ++q) {
    EXPECT_EQ(QuadrantStateForMask(0, static_cast<Quadrant>(q)), QuadrantState::kOuterCorner);
  }

  // North and west covered but the north-west diagonal missing is the concave
  // corner an L-shaped region produces, and the case a 3x3 block cannot express.
  const uint8_t inner_corner = NormalizeNeighborMask(kNorth | kWest);
  EXPECT_EQ(QuadrantStateForMask(inner_corner, Quadrant::kNorthWest), QuadrantState::kInnerCorner);
}

TEST(TerrainMaskTest, QuadrantStateSeparatesVerticalAndHorizontalEdges) {
  // Only the north neighbour: the west side of the north-west quadrant is
  // exposed, leaving a vertical wall face.
  EXPECT_EQ(QuadrantStateForMask(NormalizeNeighborMask(kNorth), Quadrant::kNorthWest),
            QuadrantState::kEdgeVertical);

  // Only the west neighbour: the north side is exposed, leaving a flat surface.
  EXPECT_EQ(QuadrantStateForMask(NormalizeNeighborMask(kWest), Quadrant::kNorthWest),
            QuadrantState::kEdgeHorizontal);
}

TEST(TerrainMaskTest, EachOffsetPointsWhereItsBitIsNamed) {
  // The generator walks a 3x3 canvas and the brush walks a level grid, both off
  // this one table. If an offset and its bit ever disagreed, generated artwork
  // and painted levels would stop matching with nothing to say so -- the two
  // used to keep private copies of this table and agreeing was a coincidence.
  struct Named {
    Neighbor bit;
    int dx;
    int dy;
  };
  constexpr Named kExpected[] = {
      {kNorth, 0, -1},    {kNorthEast, 1, -1}, {kEast, 1, 0},  {kSouthEast, 1, 1},
      {kSouth, 0, 1},     {kSouthWest, -1, 1}, {kWest, -1, 0}, {kNorthWest, -1, -1},
  };
  ASSERT_EQ(std::size(kExpected), kNeighborCount);

  for (int i = 0; i < kNeighborCount; ++i) {
    EXPECT_EQ(1 << i, kExpected[i].bit) << "bit position " << i;
    EXPECT_EQ(kNeighborOffsets[i].dx, kExpected[i].dx) << "bit position " << i;
    EXPECT_EQ(kNeighborOffsets[i].dy, kExpected[i].dy) << "bit position " << i;
  }
}

}  // namespace
}  // namespace zebes
