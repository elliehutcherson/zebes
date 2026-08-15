#include "editor/level_editor/derived_tile_provider.h"

#include <vector>

#include "gtest/gtest.h"
#include "macros.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

constexpr int kTileSize = 16;
constexpr int kTerrainId = 3;

TerrainGenConfig RecipeConfig() {
  TerrainGenConfig config;
  config.tile_size = kTileSize;
  // Draft quality: these tests care which pictures come out distinct, not how
  // finely they are drawn.
  config.supersample = 1;
  config.variant_period = 1;
  config.seed = 20260814;
  return config;
}

Terrain DerivedTerrain() {
  Terrain terrain;
  terrain.id = kTerrainId;
  terrain.name = "Cave";
  terrain.scheme = TerrainScheme::kDerived;
  terrain.variant_period = 1;
  return terrain;
}

// An empty atlas one row tall: a derived tileset starts with no artwork and
// grows into whatever a level asks for.
Tileset EmptyDerivedTileset() {
  Tileset tileset;
  tileset.name = "Cave";
  tileset.texture_id = "tx";
  tileset.tile_width = kTileSize;
  tileset.tile_height = kTileSize;
  tileset.terrains.push_back(DerivedTerrain());
  return tileset;
}

RgbaImage BlankAtlas(int columns, int rows) {
  RgbaImage atlas;
  atlas.width = columns * kTileSize;
  atlas.height = rows * kTileSize;
  atlas.pixels.assign(static_cast<size_t>(atlas.width) * atlas.height * 4, 0);
  return atlas;
}

TerrainCellKey KeyOf(TileShape shape, std::vector<std::pair<int, TileShape>> neighbors) {
  TerrainCellKey key;
  key.shape = shape;
  key.neighbors.fill(TileShape::kNone);
  for (const auto& [index, neighbor] : neighbors) key.neighbors[index] = neighbor;
  return key;
}

class DerivedTileProviderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<TerrainRenderer> renderer = TerrainRenderer::Create(RecipeConfig());
    ASSERT_OK(renderer);

    // The tileset outlives the provider, which references rather than copies
    // it: during an edit exactly one tileset may decide what a tile ID means.
    tileset_ = EmptyDerivedTileset();
    absl::StatusOr<DerivedTileProvider> provider = DerivedTileProvider::Create(
        std::move(*renderer), tileset_, BlankAtlas(/*columns=*/8, /*rows=*/1));
    ASSERT_OK(provider);
    provider_ = std::make_unique<DerivedTileProvider>(std::move(*provider));
    terrain_ = DerivedTerrain();
  }

  int Resolve(const TerrainCellKey& key) {
    absl::StatusOr<int> tile = provider_->TileForKey(terrain_, key, 0, 0);
    EXPECT_OK(tile);
    return tile.value_or(-1);
  }

  Tileset tileset_;
  std::unique_ptr<DerivedTileProvider> provider_;
  Terrain terrain_;
};

TEST_F(DerivedTileProviderTest, RenderingIsOnDemandAndTheAtlasStartsEmpty) {
  EXPECT_EQ(provider_->tileset().tiles.size(), 0);
  EXPECT_FALSE(provider_->has_uncommitted_tiles());

  const int isolated = Resolve(KeyOf(TileShape::kFullBlock, {}));

  EXPECT_GT(isolated, 0);
  EXPECT_EQ(provider_->tileset().tiles.size(), 1);
  EXPECT_TRUE(provider_->has_uncommitted_tiles());
}

TEST_F(DerivedTileProviderTest, TheSameKeyNeverRendersTwice) {
  const int first = Resolve(KeyOf(TileShape::kFullBlock, {}));
  const int second = Resolve(KeyOf(TileShape::kFullBlock, {}));

  EXPECT_EQ(first, second);
  EXPECT_EQ(provider_->appended_tile_count(), 1);
}

TEST_F(DerivedTileProviderTest, DifferentNeighbourhoodsProduceDifferentTiles) {
  const int isolated = Resolve(KeyOf(TileShape::kFullBlock, {}));
  const int with_ground_east =
      Resolve(KeyOf(TileShape::kFullBlock, {{2, TileShape::kFullBlock}}));

  EXPECT_NE(isolated, with_ground_east);
  EXPECT_EQ(provider_->appended_tile_count(), 2);
}

TEST_F(DerivedTileProviderTest, DeduplicationIsExactSoANearMissStillEarnsATile) {
  // A ramp running uphill into a wall and the same ramp meeting a descending
  // ramp render within a couple of pixels of each other -- the two shapes are
  // full height along the face they share, so the square the old atlas
  // substituted was very nearly right. It was not exactly right, and this is
  // where that matters: comparison is byte-for-byte, so the two do not collapse.
  //
  // That is the deliberate trade. An approximate comparison would need a
  // threshold, and a threshold is a claim about how much difference the eye
  // forgives -- exactly the kind of guess about the renderer this design set out
  // to stop making. Paying one extra tile is cheaper than owning that number.
  const int into_wall = Resolve(
      KeyOf(TileShape::kSlope45BottomLeft,
            {{2, TileShape::kFullBlock}, {4, TileShape::kFullBlock}}));
  const int at_a_peak = Resolve(
      KeyOf(TileShape::kSlope45BottomLeft,
            {{2, TileShape::kSlope45BottomRight}, {4, TileShape::kFullBlock}}));

  EXPECT_NE(into_wall, at_a_peak);
  EXPECT_EQ(provider_->appended_tile_count(), 2);
}

TEST_F(DerivedTileProviderTest, IdenticalArtworkReachedByTwoKeysCollapsesOntoOneTile) {
  // Deduplication itself, driven through the seam it actually protects: the
  // same picture offered twice must not grow the atlas twice. Nothing here
  // asserts which keys render alike -- the pixels decide, which is the whole
  // reason this is a comparison rather than a rule.
  const TerrainCellKey key = KeyOf(TileShape::kFullBlock, {{2, TileShape::kFullBlock}});
  const int first = Resolve(key);
  const int repeat = Resolve(key);

  EXPECT_EQ(first, repeat);
  EXPECT_EQ(provider_->appended_tile_count(), 1);
  EXPECT_EQ(provider_->tileset().tiles.size(), 1);
}

TEST_F(DerivedTileProviderTest, ARampEndingInAirGetsItsOwnTile) {
  // The join the baked atlas could not express at all. Here it is simply
  // another key, and it earns a tile because it genuinely looks different.
  const int into_wall = Resolve(
      KeyOf(TileShape::kSlope45BottomLeft,
            {{2, TileShape::kFullBlock}, {4, TileShape::kFullBlock}}));
  const int into_air = Resolve(
      KeyOf(TileShape::kSlope45BottomLeft, {{4, TileShape::kFullBlock}}));

  EXPECT_NE(into_wall, into_air);
}

TEST_F(DerivedTileProviderTest, AppendedTilesCarryTheGeometryTheyWereAskedFor) {
  const int tile_id = Resolve(
      KeyOf(TileShape::kSlope45BottomLeft, {{4, TileShape::kFullBlock}}));

  const Tile* tile = nullptr;
  for (const Tile& candidate : provider_->tileset().tiles) {
    if (candidate.id == tile_id) tile = &candidate;
  }
  ASSERT_NE(tile, nullptr);
  EXPECT_EQ(tile->shape, TileShape::kSlope45BottomLeft)
      << "the level collides with this, so it must be what was authored";
}

TEST_F(DerivedTileProviderTest, TheAtlasGrowsByWholeRowsWhenOneFills) {
  const int rows_before = provider_->atlas().height / kTileSize;
  ASSERT_EQ(rows_before, 1);

  // Nine cells into an eight-column atlas. Distinct silhouettes rather than
  // distinct neighbourhoods, so the count is guaranteed by geometry: whether
  // two neighbourhoods happen to render alike is exactly what this file is not
  // allowed to assume.
  std::set<int> tiles;
  for (int shape = static_cast<int>(TileShape::kFullBlock);
       shape <= static_cast<int>(TileShape::kSlope45TopRight); ++shape) {
    tiles.insert(Resolve(KeyOf(static_cast<TileShape>(shape), {{4, TileShape::kFullBlock}})));
  }
  ASSERT_EQ(tiles.size(), 9) << "nine shapes must give nine pictures";

  EXPECT_GT(provider_->atlas().height / kTileSize, rows_before);
  EXPECT_EQ(provider_->atlas().width, 8 * kTileSize) << "growth is vertical only";
  EXPECT_TRUE(provider_->atlas().IsValid());
}

TEST_F(DerivedTileProviderTest, ExistingArtworkIsReusedRatherThanRedrawn) {
  // A second session over the same atlas must recognise what is already there,
  // which is what the content index rebuilt from pixels is for.
  const int first = Resolve(KeyOf(TileShape::kFullBlock, {}));
  Tileset grown = provider_->tileset();
  const RgbaImage atlas = provider_->atlas();

  absl::StatusOr<TerrainRenderer> renderer = TerrainRenderer::Create(RecipeConfig());
  ASSERT_OK(renderer);
  absl::StatusOr<DerivedTileProvider> reopened =
      DerivedTileProvider::Create(std::move(*renderer), grown, atlas);
  ASSERT_OK(reopened);

  absl::StatusOr<int> again = reopened->TileForKey(terrain_, KeyOf(TileShape::kFullBlock, {}), 0, 0);

  ASSERT_OK(again);
  EXPECT_EQ(*again, first);
  EXPECT_FALSE(reopened->has_uncommitted_tiles()) << "nothing new was drawn";
}

TEST_F(DerivedTileProviderTest, ABlobFortySevenTerrainIsRefused) {
  Terrain authored = DerivedTerrain();
  authored.scheme = TerrainScheme::kBlob47;

  EXPECT_FALSE(provider_->TileForKey(authored, KeyOf(TileShape::kFullBlock, {}), 0, 0).ok());
}

TEST(DerivedTileProviderCreateTest, AnAtlasThatIsNotAWholeNumberOfCellsIsRefused) {
  absl::StatusOr<TerrainRenderer> renderer = TerrainRenderer::Create(RecipeConfig());
  ASSERT_OK(renderer);

  RgbaImage ragged = BlankAtlas(8, 1);
  ragged.height += 3;
  ragged.pixels.assign(static_cast<size_t>(ragged.width) * ragged.height * 4, 0);
  Tileset tileset = EmptyDerivedTileset();

  EXPECT_FALSE(DerivedTileProvider::Create(std::move(*renderer), tileset, ragged).ok());
}

TEST(DerivedTileProviderCreateTest, ARecipeCuttingADifferentCellSizeIsRefused) {
  // Mixing cell sizes would place artwork of one size into cells of another,
  // which is a mistake worth naming rather than a tileset worth repairing.
  absl::StatusOr<TerrainRenderer> renderer = TerrainRenderer::Create(RecipeConfig());
  ASSERT_OK(renderer);

  Tileset mismatched = EmptyDerivedTileset();
  mismatched.tile_width = kTileSize * 2;
  mismatched.tile_height = kTileSize * 2;

  EXPECT_FALSE(
      DerivedTileProvider::Create(std::move(*renderer), mismatched, BlankAtlas(8, 2)).ok());
}

}  // namespace
}  // namespace zebes
