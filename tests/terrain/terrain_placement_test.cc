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

std::vector<std::string> NamesOf(const std::vector<TerrainShapeChoice>& choices) {
  std::vector<std::string> names;
  for (const TerrainShapeChoice& choice : choices) names.push_back(choice.name);
  return names;
}

TEST(TerrainPlacementTest, EveryShapeIsOfferedExactlyOnce) {
  // The guard that keeps this honest as shapes are added. A TileShape with no
  // entry is unreachable from the palette; one with two entries makes which you
  // get depend on iteration order. Both fail here rather than in someone's
  // level.
  std::set<TileShape> offered;
  for (const TerrainShapeChoice& choice : AllTerrainShapeChoices()) {
    EXPECT_NE(choice.shape, TileShape::kNone) << "air is not paintable";
    EXPECT_FALSE(choice.name.empty());
    EXPECT_TRUE(offered.insert(choice.shape).second)
        << kTileShapeIdentifiers[static_cast<size_t>(choice.shape)] << " is offered twice";
  }

  for (int i = 1; i <= static_cast<int>(TileShape::kSteepSlopeTopRight_Top); ++i) {
    EXPECT_TRUE(offered.contains(static_cast<TileShape>(i)))
        << kTileShapeIdentifiers[i] << " cannot be painted";
  }
}

TEST(TerrainPlacementTest, EachHalfOfATwoCellRampIsItsOwnChoice) {
  // The two halves are placed separately on purpose. Stamping both at once
  // would make a ramp with a landing in the middle unreachable -- lower half,
  // flat half blocks, upper half -- and that arrangement is continuous because
  // every one of those pieces meets its neighbour at half tile height.
  const std::vector<std::string> names = NamesOf(ShapeChoicesWithin(EveryShape()));

  EXPECT_THAT(names, Contains("Gentle floor, up to the right, lower half"));
  EXPECT_THAT(names, Contains("Gentle floor, up to the right, upper half"));
  EXPECT_THAT(names, Contains("Half block, floor"));
}

TEST(TerrainPlacementTest, ADerivedTerrainOffersTheWholeCatalogue) {
  EXPECT_EQ(ShapeChoicesWithin(EveryShape()).size(), AllTerrainShapeChoices().size());
}

TEST(TerrainPlacementTest, ATerrainWithOnlyBlocksOffersOnlyTheBlock) {
  // What a blob-47 terrain with no slope artwork can paint.
  const std::vector<TerrainShapeChoice> choices = ShapeChoicesWithin({TileShape::kFullBlock});

  ASSERT_EQ(choices.size(), 1);
  EXPECT_EQ(choices.front().shape, TileShape::kFullBlock);
  EXPECT_EQ(choices.front().name, "Block");
}

TEST(TerrainPlacementTest, AShapeWithNoArtworkIsNotOffered) {
  const std::vector<std::string> names = NamesOf(ShapeChoicesWithin(
      {TileShape::kFullBlock, TileShape::kGentleSlopeBottomLeft_Lower}));

  EXPECT_THAT(names, Contains("Gentle floor, up to the right, lower half"));
  EXPECT_THAT(names, Not(Contains("Gentle floor, up to the right, upper half")));
}

TEST(TerrainPlacementTest, TheCatalogueLeadsWithTheBlock) {
  // The overwhelmingly common choice should not need hunting for.
  ASSERT_FALSE(AllTerrainShapeChoices().empty());
  EXPECT_EQ(AllTerrainShapeChoices().front().shape, TileShape::kFullBlock);
}

}  // namespace
}  // namespace zebes
