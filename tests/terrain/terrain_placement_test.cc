#include "terrain/terrain_placement.h"

#include <set>

#include "absl/container/flat_hash_set.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace zebes {
namespace {

using ::testing::Contains;
using ::testing::Not;

absl::flat_hash_set<TileShape> EveryShape() {
  absl::flat_hash_set<TileShape> shapes;
  for (int i = 1; i <= static_cast<int>(TileShape::kSteepSlopeTopRight_Top); ++i) {
    shapes.insert(static_cast<TileShape>(i));
  }
  return shapes;
}

std::vector<std::string> NamesOf(const std::vector<TerrainPlacementUnit>& units) {
  std::vector<std::string> names;
  for (const TerrainPlacementUnit& unit : units) names.push_back(unit.name);
  return names;
}

TEST(TerrainPlacementTest, EveryShapeBelongsToExactlyOneUnit) {
  // The guard that keeps this future-proof. Adding a TileShape without giving
  // it a placement unit leaves it unreachable from the palette, and adding it
  // to two units makes which one you get depend on iteration order. Both fail
  // here rather than in someone's level.
  std::set<TileShape> claimed;
  for (const TerrainPlacementUnit& unit : AllTerrainPlacementUnits()) {
    for (const TileShape cell : unit.cells) {
      EXPECT_NE(cell, TileShape::kNone) << "unit '" << unit.name << "' claims an empty cell";
      EXPECT_TRUE(claimed.insert(cell).second)
          << kTileShapeIdentifiers[static_cast<size_t>(cell)] << " is claimed twice";
    }
  }

  for (int i = 1; i <= static_cast<int>(TileShape::kSteepSlopeTopRight_Top); ++i) {
    const TileShape shape = static_cast<TileShape>(i);
    EXPECT_TRUE(claimed.contains(shape))
        << kTileShapeIdentifiers[i] << " has no placement unit and cannot be placed";
  }
}

TEST(TerrainPlacementTest, ACellCountAlwaysMatchesTheUnitsFootprint) {
  for (const TerrainPlacementUnit& unit : AllTerrainPlacementUnits()) {
    EXPECT_GT(unit.width, 0) << unit.name;
    EXPECT_GT(unit.height, 0) << unit.name;
    EXPECT_EQ(unit.cells.size(), static_cast<size_t>(unit.width) * unit.height) << unit.name;
    EXPECT_FALSE(unit.name.empty());
  }
}

TEST(TerrainPlacementTest, GentleRampsAreTwoCellsWideAndSteepRampsTwoTall) {
  // The reason units exist at all: these are one thing the author places, not
  // two things they have to remember to pair up.
  for (const TerrainPlacementUnit& unit : AllTerrainPlacementUnits()) {
    if (unit.name.find("Gentle") == 0) {
      EXPECT_EQ(unit.width, 2) << unit.name;
      EXPECT_EQ(unit.height, 1) << unit.name;
    }
    if (unit.name.find("Steep") == 0) {
      EXPECT_EQ(unit.width, 1) << unit.name;
      EXPECT_EQ(unit.height, 2) << unit.name;
    }
  }
}

TEST(TerrainPlacementTest, ADerivedTerrainOffersTheWholeCatalogue) {
  const std::vector<TerrainPlacementUnit> units = PlacementUnitsWithin(EveryShape());

  EXPECT_EQ(units.size(), AllTerrainPlacementUnits().size());
}

TEST(TerrainPlacementTest, ATerrainWithOnlyBlocksOffersOnlyTheBlock) {
  // What a blob-47 terrain with no slope artwork can place.
  const std::vector<TerrainPlacementUnit> units =
      PlacementUnitsWithin({TileShape::kFullBlock});

  ASSERT_EQ(units.size(), 1);
  EXPECT_EQ(units.front().name, "Block");
  EXPECT_EQ(units.front().cells, std::vector<TileShape>{TileShape::kFullBlock});
}

TEST(TerrainPlacementTest, ARampMissingOneHalfIsNotOfferedAtAll) {
  // All or nothing. Offering a ramp whose second cell has no artwork would let
  // the author place collision geometry with a hole in its picture.
  absl::flat_hash_set<TileShape> partial = {
      TileShape::kFullBlock,
      TileShape::kGentleSlopeBottomLeft_Lower,
  };

  const std::vector<TerrainPlacementUnit> units = PlacementUnitsWithin(partial);

  EXPECT_THAT(NamesOf(units), Not(Contains("Gentle floor, up to the right")));
  EXPECT_THAT(NamesOf(units), Contains("Block"));
}

TEST(TerrainPlacementTest, BothHalvesPresentOffersTheRamp) {
  absl::flat_hash_set<TileShape> complete = {
      TileShape::kGentleSlopeBottomLeft_Lower,
      TileShape::kGentleSlopeBottomLeft_Upper,
  };

  EXPECT_THAT(NamesOf(PlacementUnitsWithin(complete)),
              Contains("Gentle floor, up to the right"));
}

TEST(TerrainPlacementTest, AGentleRampsLowerHalfLeadsWhenItRisesToTheRight) {
  // Pinning the orientation, because it is the one thing here that cannot be
  // derived from the names and is easy to mirror by accident.
  for (const TerrainPlacementUnit& unit : AllTerrainPlacementUnits()) {
    if (unit.name != "Gentle floor, up to the right") continue;

    EXPECT_EQ(unit.At(0, 0), TileShape::kGentleSlopeBottomLeft_Lower);
    EXPECT_EQ(unit.At(1, 0), TileShape::kGentleSlopeBottomLeft_Upper);
    return;
  }
  FAIL() << "the catalogue has no gentle floor ramp rising to the right";
}

TEST(TerrainPlacementTest, ASteepRampStacksItsTopHalfAbove) {
  for (const TerrainPlacementUnit& unit : AllTerrainPlacementUnits()) {
    if (unit.name != "Steep floor, up to the right") continue;

    EXPECT_EQ(unit.At(0, 0), TileShape::kSteepSlopeBottomLeft_Top);
    EXPECT_EQ(unit.At(0, 1), TileShape::kSteepSlopeBottomLeft_Bottom);
    return;
  }
  FAIL() << "the catalogue has no steep floor ramp rising to the right";
}

TEST(TerrainPlacementTest, ReadingOutsideAUnitIsAir) {
  const TerrainPlacementUnit& block = AllTerrainPlacementUnits().front();

  EXPECT_EQ(block.At(1, 0), TileShape::kNone);
  EXPECT_EQ(block.At(0, -1), TileShape::kNone);
}

}  // namespace
}  // namespace zebes
