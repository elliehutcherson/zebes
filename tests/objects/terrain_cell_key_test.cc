#include <string>

#include "absl/container/flat_hash_set.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/tileset.h"

namespace zebes {
namespace {

using ::testing::HasSubstr;

TerrainCellKey GroundKey() {
  TerrainCellKey key;
  key.shape = TileShape::kFullBlock;
  key.neighbors.fill(TileShape::kFullBlock);
  return key;
}

TEST(TerrainCellKeyTest, KeysDifferingOnlyInANeighboursShapeAreDistinct) {
  // The whole point of the type. The 47-mask calls these two the same cell,
  // which is why a ramp beside ground and a ramp beside air used to share one
  // drawing.
  TerrainCellKey with_block = GroundKey();
  TerrainCellKey with_ramp = GroundKey();
  with_ramp.neighbors[2] = TileShape::kSlope45BottomLeft;

  EXPECT_NE(with_block, with_ramp);
  EXPECT_NE(absl::HashOf(with_block), absl::HashOf(with_ramp));
}

TEST(TerrainCellKeyTest, PhaseIsPartOfTheKey) {
  TerrainCellKey first = GroundKey();
  TerrainCellKey second = GroundKey();
  second.phase = 1;

  EXPECT_NE(first, second);
}

TEST(TerrainCellKeyTest, KeysAreUsableAsHashTableEntries) {
  absl::flat_hash_set<TerrainCellKey> keys;

  TerrainCellKey ramp = GroundKey();
  ramp.shape = TileShape::kSlope45BottomLeft;

  EXPECT_TRUE(keys.insert(GroundKey()).second);
  EXPECT_TRUE(keys.insert(ramp).second);
  EXPECT_FALSE(keys.insert(GroundKey()).second);
  EXPECT_EQ(keys.size(), 2);
}

TEST(TerrainCellKeyTest, TheMaskProjectionMatchesTheOldSchemesQuestion) {
  // NeighborMaskOf is what a blob-47 terrain keys on: solid or not, per
  // neighbour. A wedge and a full block both read as solid, which is exactly
  // the information loss that made this type necessary.
  TerrainCellKey key = GroundKey();
  key.neighbors.fill(TileShape::kNone);
  key.neighbors[0] = TileShape::kFullBlock;
  key.neighbors[2] = TileShape::kSlope45BottomLeft;

  EXPECT_EQ(NeighborMaskOf(key), kNorth | kEast);
}

TEST(TerrainCellKeyTest, AnAllAirNeighbourhoodProjectsToAnEmptyMask) {
  TerrainCellKey key;
  key.shape = TileShape::kFullBlock;
  key.neighbors.fill(TileShape::kNone);

  EXPECT_EQ(NeighborMaskOf(key), 0);
}

TEST(TerrainCellKeyTest, DebugStringNamesShapesRatherThanNumbers) {
  TerrainCellKey key = GroundKey();
  key.shape = TileShape::kSlope45BottomLeft;
  key.neighbors[4] = TileShape::kGentleSlopeBottomLeftUpper;

  const std::string described = DebugString(key);

  EXPECT_THAT(described, HasSubstr("kSlope45BottomLeft"));
  EXPECT_THAT(described, HasSubstr("kGentleSlopeBottomLeftUpper"));
}

}  // namespace
}  // namespace zebes
