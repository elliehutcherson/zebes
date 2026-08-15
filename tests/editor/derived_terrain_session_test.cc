#include "editor/level_editor/derived_terrain_session.h"

#include <vector>

#include "api_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

constexpr int kTileSize = 16;
constexpr char kTextureId[] = "atlas-texture";
constexpr char kTilesetId[] = "cave-tileset";

TerrainGenConfig RecipeConfig() {
  TerrainGenConfig config;
  config.tile_size = kTileSize;
  config.supersample = 1;
  config.variant_period = 1;
  config.seed = 20260814;
  return config;
}

Tileset DerivedTileset() {
  Tileset tileset;
  tileset.id = kTilesetId;
  tileset.name = "Cave";
  tileset.texture_id = kTextureId;
  tileset.tile_width = kTileSize;
  tileset.tile_height = kTileSize;

  Terrain terrain;
  terrain.id = 1;
  terrain.name = "Cave";
  terrain.scheme = TerrainScheme::kDerived;
  terrain.variant_period = 1;
  tileset.terrains.push_back(std::move(terrain));
  return tileset;
}

Tileset AuthoredTileset() {
  Tileset tileset = DerivedTileset();
  tileset.terrains[0].scheme = TerrainScheme::kBlob47;
  return tileset;
}

RgbaImage BlankAtlas() {
  RgbaImage atlas;
  atlas.width = 8 * kTileSize;
  atlas.height = kTileSize;
  atlas.pixels.assign(static_cast<size_t>(atlas.width) * atlas.height * 4, 0);
  return atlas;
}

TerrainRecipe RecipeFor(const std::string& tileset_id) {
  TerrainRecipe recipe;
  recipe.id = "recipe";
  recipe.name = "Cave";
  recipe.tileset_id = tileset_id;
  recipe.texture_id = kTextureId;
  recipe.terrain_id = 1;
  recipe.config = RecipeConfig();
  return recipe;
}

TerrainCellKey GroundKey() {
  TerrainCellKey key;
  key.shape = TileShape::kFullBlock;
  key.neighbors.fill(TileShape::kNone);
  return key;
}

class DerivedTerrainSessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ON_CALL(api_, FindTerrainRecipeForTileset(kTilesetId))
        .WillByDefault(Return(std::optional<TerrainRecipe>(RecipeFor(kTilesetId))));
    ON_CALL(api_, ReadTexturePixels(kTextureId)).WillByDefault([] { return BlankAtlas(); });
    ON_CALL(api_, ShowTexturePixels).WillByDefault(Return(absl::OkStatus()));
    ON_CALL(api_, ReplaceTexturePixels).WillByDefault(Return(absl::OkStatus()));
    ON_CALL(api_, UpdateTileset).WillByDefault(Return(absl::OkStatus()));
  }

  // Paints one cell's worth of artwork through the session's provider.
  void ResolveOneTile(Tileset& tileset) {
    ASSERT_NE(session_.provider(), nullptr);
    ASSERT_OK(session_.provider()->TileForKey(tileset.terrains[0], GroundKey(), 0, 0));
  }

  NiceMock<MockApi> api_;
  DerivedTerrainSession session_;
};

TEST_F(DerivedTerrainSessionTest, ATilesetWithNoDerivedTerrainLeavesTheSessionClosed) {
  Tileset authored = AuthoredTileset();

  ASSERT_OK(session_.OpenFor(api_, authored));

  EXPECT_FALSE(session_.is_open());
  EXPECT_EQ(session_.provider(), nullptr) << "the authored provider handles this tileset";
}

TEST_F(DerivedTerrainSessionTest, ADerivedTerrainWithNoRecipeIsRefused) {
  // Its artwork cannot be rendered, so painting it would silently do nothing.
  EXPECT_CALL(api_, FindTerrainRecipeForTileset(kTilesetId))
      .WillOnce(Return(std::optional<TerrainRecipe>()));
  Tileset tileset = DerivedTileset();

  const absl::Status status = session_.OpenFor(api_, tileset);

  EXPECT_TRUE(absl::IsFailedPrecondition(status)) << status;
  EXPECT_FALSE(session_.is_open());
}

TEST_F(DerivedTerrainSessionTest, ReopeningTheSameTilesetKeepsWhatWasRendered) {
  // The session carries a content index and a per-session memo. Rebuilding it
  // every frame would re-read the atlas and re-render everything already drawn.
  Tileset tileset = DerivedTileset();
  ASSERT_OK(session_.OpenFor(api_, tileset));
  ResolveOneTile(tileset);
  ASSERT_EQ(tileset.tiles.size(), 1);

  EXPECT_CALL(api_, ReadTexturePixels).Times(0);
  ASSERT_OK(session_.OpenFor(api_, tileset));

  EXPECT_TRUE(session_.is_open());
  EXPECT_EQ(tileset.tiles.size(), 1) << "nothing was re-rendered";
}

TEST_F(DerivedTerrainSessionTest, NewArtworkIsShownButNotWritten) {
  // The split the whole design rests on: a painted cell must be visible at once,
  // and nothing durable happens until the level is saved.
  Tileset tileset = DerivedTileset();
  ASSERT_OK(session_.OpenFor(api_, tileset));
  ResolveOneTile(tileset);

  EXPECT_CALL(api_, ShowTexturePixels(kTextureId, _, _, _)).Times(1);
  EXPECT_CALL(api_, ReplaceTexturePixels).Times(0);
  EXPECT_CALL(api_, UpdateTileset).Times(0);

  ASSERT_OK(session_.ShowNewArtwork(api_));
  EXPECT_TRUE(session_.has_unsaved_artwork());
}

TEST_F(DerivedTerrainSessionTest, ShowingTwiceUploadsOnceWhenNothingWasAdded) {
  Tileset tileset = DerivedTileset();
  ASSERT_OK(session_.OpenFor(api_, tileset));
  ResolveOneTile(tileset);

  EXPECT_CALL(api_, ShowTexturePixels).Times(1);

  ASSERT_OK(session_.ShowNewArtwork(api_));
  ASSERT_OK(session_.ShowNewArtwork(api_));
}

TEST_F(DerivedTerrainSessionTest, CommitWritesTheAtlasBeforeTheTileset) {
  // A tileset naming artwork the atlas does not hold is worse than artwork
  // nothing names, so the ordering is asserted rather than assumed.
  Tileset tileset = DerivedTileset();
  ASSERT_OK(session_.OpenFor(api_, tileset));
  ResolveOneTile(tileset);

  std::vector<std::string> order;
  EXPECT_CALL(api_, ReplaceTexturePixels(kTextureId, _, _, _))
      .WillOnce([&order](const std::string&, int, int, absl::Span<const uint8_t>) {
        order.push_back("atlas");
        return absl::OkStatus();
      });
  EXPECT_CALL(api_, UpdateTileset).WillOnce([&order](Tileset written) {
    order.push_back("tileset");
    EXPECT_EQ(written.tiles.size(), 1) << "the grown tileset is what gets written";
    return absl::OkStatus();
  });

  ASSERT_OK(session_.Commit(api_));

  EXPECT_EQ(order, (std::vector<std::string>{"atlas", "tileset"}));
  EXPECT_FALSE(session_.has_unsaved_artwork());
}

TEST_F(DerivedTerrainSessionTest, CommittingWithNothingNewWritesNothing) {
  Tileset tileset = DerivedTileset();
  ASSERT_OK(session_.OpenFor(api_, tileset));

  EXPECT_CALL(api_, ReplaceTexturePixels).Times(0);
  EXPECT_CALL(api_, UpdateTileset).Times(0);

  ASSERT_OK(session_.Commit(api_));
}

TEST_F(DerivedTerrainSessionTest, TheTilesetGrowsInPlaceSoOneObjectDecidesTileIds) {
  // The provider references the caller's tileset. A copy would let the viewport
  // resolve an ID the brush had just invented, or fail to resolve one it had.
  Tileset tileset = DerivedTileset();
  ASSERT_OK(session_.OpenFor(api_, tileset));
  ASSERT_TRUE(tileset.tiles.empty());

  ResolveOneTile(tileset);

  ASSERT_EQ(tileset.tiles.size(), 1);
  EXPECT_EQ(tileset.tiles.front().shape, TileShape::kFullBlock);
}

// The atlas grows in memory and reaches disk only on save, so neither the file
// nor the Tileset Editor shows it happening. Without this the one behaviour the
// derived-artwork phase is about could not be checked by looking.
TEST_F(DerivedTerrainSessionTest, ArtworkStatusReportsGrowthAndWhatIsUnsaved) {
  Tileset tileset = DerivedTileset();
  ASSERT_OK(session_.OpenFor(api_, tileset));

  const DerivedTerrainSession::ArtworkStatus before = session_.artwork_status();
  EXPECT_EQ(before.tiles, 0);
  EXPECT_EQ(before.unsaved, 0);
  EXPECT_GT(before.atlas_width, 0);

  ResolveOneTile(tileset);

  const DerivedTerrainSession::ArtworkStatus after = session_.artwork_status();
  EXPECT_EQ(after.tiles, 1);
  EXPECT_EQ(after.unsaved, 1) << "painted but not written";

  ASSERT_OK(session_.Commit(api_));

  const DerivedTerrainSession::ArtworkStatus committed = session_.artwork_status();
  EXPECT_EQ(committed.tiles, 1);
  EXPECT_EQ(committed.unsaved, 0) << "saving is what clears it";
}

TEST_F(DerivedTerrainSessionTest, AClosedSessionReportsNoArtwork) {
  const DerivedTerrainSession::ArtworkStatus status = session_.artwork_status();

  EXPECT_EQ(status.tiles, 0);
  EXPECT_EQ(status.atlas_width, 0);
  EXPECT_EQ(status.unsaved, 0);
}

}  // namespace
}  // namespace zebes
