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
  for (int i = 1; i <= static_cast<int>(TileShape::kSteepSlopeCeilingTallLeftTop); ++i) {
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

  for (int i = 1; i <= static_cast<int>(TileShape::kSteepSlopeCeilingTallLeftTop); ++i) {
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
  const std::vector<std::string> names = NamesOf(
      ShapeChoicesWithin({TileShape::kFullBlock, TileShape::kGentleSlopeFloorTallRightLower}));

  EXPECT_THAT(names, Contains("Gentle floor, up to the right, lower half"));
  EXPECT_THAT(names, Not(Contains("Gentle floor, up to the right, upper half")));
}

TEST(TerrainPlacementTest, TheCatalogueLeadsWithTheBlock) {
  // The overwhelmingly common choice should not need hunting for.
  ASSERT_FALSE(AllTerrainShapeChoices().empty());
  EXPECT_EQ(AllTerrainShapeChoices().front().shape, TileShape::kFullBlock);
}

// A grid lays the catalogue out by family, which is what makes a two-cell ramp
// legible: its halves sit next to each other instead of two rows apart in a
// list of twenty-five names.
TEST(TerrainPlacementTest, EveryChoiceNamesTheFamilyItBelongsTo) {
  for (const TerrainShapeChoice& choice : AllTerrainShapeChoices()) {
    EXPECT_FALSE(choice.group.empty()) << choice.name << " has no group";
  }
}

TEST(TerrainPlacementTest, ChoicesInAGroupAreContiguousSoRowsDoNotRepeat) {
  std::set<std::string> started;
  std::string current;
  for (const TerrainShapeChoice& choice : AllTerrainShapeChoices()) {
    if (choice.group == current) continue;
    EXPECT_TRUE(started.insert(choice.group).second)
        << choice.group << " is split across the catalogue, so a grid would draw it twice";
    current = choice.group;
  }
}

// --- Swatch selection --------------------------------------------------------

TerrainCellKey SurroundedBlockKey() {
  TerrainCellKey key;
  key.shape = TileShape::kFullBlock;
  key.neighbors.fill(TileShape::kFullBlock);
  return key;
}

TEST(TerrainSwatchTileTest, ABlobFortySevenTerrainUsesItsSolidMaskRule) {
  Tileset tileset;
  tileset.tiles = {Tile{.id = 4, .name = "edge"}, Tile{.id = 9, .name = "interior"}};
  Terrain terrain;
  terrain.scheme = TerrainScheme::kBlob47;
  terrain.rules = {
      TerrainRule{.mask = 1, .variants = {TerrainVariant{.tile_id = 4}}},
      TerrainRule{.mask = 255, .variants = {TerrainVariant{.tile_id = 9}}},
  };

  const Tile* swatch = TerrainSwatchTile(tileset, terrain);

  ASSERT_NE(swatch, nullptr);
  EXPECT_EQ(swatch->id, 9) << "the interior reads as the material, not one of its edges";
}

// The bug this function was extracted for: a derived terrain has no rule table
// by design, so scanning rules alone found nothing and the palette drew a grey
// box for every derived terrain, forever.
TEST(TerrainSwatchTileTest, ADerivedTerrainUsesTheTileWhoseKeyIsSurrounded) {
  Tileset tileset;
  tileset.tiles = {Tile{.id = 2, .name = "corner"}, Tile{.id = 6, .name = "interior"}};
  Terrain terrain;
  terrain.scheme = TerrainScheme::kDerived;
  TerrainCellKey corner;
  corner.shape = TileShape::kFullBlock;
  corner.neighbors.fill(TileShape::kNone);
  terrain.derived_tiles = {
      DerivedTile{.tile_id = 2, .key = corner},
      DerivedTile{.tile_id = 6, .key = SurroundedBlockKey()},
  };

  const Tile* swatch = TerrainSwatchTile(tileset, terrain);

  ASSERT_NE(swatch, nullptr);
  EXPECT_EQ(swatch->id, 6);
}

TEST(TerrainSwatchTileTest, ADerivedTerrainFallsBackToAnyArtworkItHas) {
  // Early in a level nothing interior has been painted yet. Showing an edge
  // piece is a worse picture of the material than the interior, and a much
  // better one than a blank.
  Tileset tileset;
  tileset.tiles = {Tile{.id = 2, .name = "corner"}};
  Terrain terrain;
  terrain.scheme = TerrainScheme::kDerived;
  TerrainCellKey corner;
  corner.shape = TileShape::kFullBlock;
  corner.neighbors.fill(TileShape::kNone);
  terrain.derived_tiles = {DerivedTile{.tile_id = 2, .key = corner}};

  const Tile* swatch = TerrainSwatchTile(tileset, terrain);

  ASSERT_NE(swatch, nullptr);
  EXPECT_EQ(swatch->id, 2);
}

TEST(TerrainSwatchTileTest, ATerrainThatHasDrawnNothingHasNoSwatch) {
  Tileset tileset;
  Terrain terrain;
  terrain.scheme = TerrainScheme::kDerived;

  EXPECT_EQ(TerrainSwatchTile(tileset, terrain), nullptr);
}

}  // namespace
}  // namespace zebes
