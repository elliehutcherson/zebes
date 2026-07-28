#include "editor/level_editor/terrain_brush.h"

#include <set>

#include "editor/level_editor/viewport_model.h"
#include "gtest/gtest.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

constexpr int kTerrainId = 5;
constexpr int kTileSize = 16;

// Tile IDs are laid out so a tile ID decodes back to its mask: the tile for
// table index i is kFirstTileId + i. Tests can therefore assert on the mask a
// cell resolved to by reading the level directly.
constexpr int kFirstTileId = 1;

int TileIdForMask(uint8_t mask) {
  std::optional<int> index = Blob47IndexForMask(mask);
  EXPECT_TRUE(index.has_value()) << "mask " << static_cast<int>(mask) << " is not normalized";
  return kFirstTileId + index.value_or(0);
}

uint8_t MaskForTileId(int tile_id) {
  const int index = tile_id - kFirstTileId;
  EXPECT_GE(index, 0);
  EXPECT_LT(index, kBlob47TileCount);
  return Blob47MaskTable()[index];
}

// A complete blob-47 terrain with one variant per mask.
Terrain MakeTerrain(bool solid_outside_level = false) {
  Terrain terrain;
  terrain.id = kTerrainId;
  terrain.name = "Grass";
  terrain.solid_outside_level = solid_outside_level;

  absl::Span<const uint8_t> masks = Blob47MaskTable();
  for (int i = 0; i < kBlob47TileCount; ++i) {
    terrain.rules.push_back(TerrainRule{
        .mask = masks[i],
        .variants = {TerrainVariant{.tile_id = kFirstTileId + i, .weight = 1}},
    });
  }
  return terrain;
}

Tileset MakeTileset(Terrain terrain) {
  Tileset tileset;
  tileset.name = "Generated";
  tileset.texture_id = "tx";
  tileset.tile_width = kTileSize;
  tileset.tile_height = kTileSize;
  for (int i = 0; i < kBlob47TileCount; ++i) {
    tileset.tiles.push_back(Tile{.id = kFirstTileId + i, .name = "T"});
  }
  tileset.terrains.push_back(std::move(terrain));
  return tileset;
}

Level MakeLevel(int tiles_wide = 16, int tiles_high = 16) {
  Level level;
  level.tile_render_width = kTileSize;
  level.tile_render_height = kTileSize;
  level.width = tiles_wide * kTileSize;
  level.height = tiles_high * kTileSize;
  return level;
}

// Reads the mask a cell currently depicts.
uint8_t MaskAt(const Level& level, int x, int y) {
  absl::StatusOr<int> tile = GetTileAt(level, x, y);
  EXPECT_TRUE(tile.ok()) << tile.status();
  return MaskForTileId(*tile);
}

// --- TerrainIndex ------------------------------------------------------------

TEST(TerrainIndexTest, ResolvesTilesAndTerrainsById) {
  Tileset tileset = MakeTileset(MakeTerrain());
  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset);
  ASSERT_TRUE(index.ok()) << index.status();

  ASSERT_NE(index->FindById(kTerrainId), nullptr);
  EXPECT_EQ(index->FindById(kTerrainId)->name, "Grass");
  EXPECT_EQ(index->FindById(999), nullptr);

  EXPECT_EQ(index->FindByTileId(kFirstTileId), index->FindById(kTerrainId));
  EXPECT_EQ(index->FindByTileId(0), nullptr) << "tile 0 is the empty cell";
  EXPECT_EQ(index->FindByTileId(9999), nullptr);
}

TEST(TerrainIndexTest, RejectsTileClaimedByTwoTerrains) {
  Tileset tileset = MakeTileset(MakeTerrain());
  Terrain other = MakeTerrain();
  other.id = kTerrainId + 1;
  other.name = "Stone";
  tileset.terrains.push_back(std::move(other));

  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset);
  ASSERT_FALSE(index.ok());
  EXPECT_EQ(index.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(TerrainIndexTest, RejectsDuplicateTerrainId) {
  Tileset tileset;
  tileset.terrains.push_back(Terrain{.id = 1, .name = "A"});
  tileset.terrains.push_back(Terrain{.id = 1, .name = "B"});

  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset);
  ASSERT_FALSE(index.ok());
  EXPECT_EQ(index.status().code(), absl::StatusCode::kInvalidArgument);
}

// --- Mask computation --------------------------------------------------------

class TerrainBrushTest : public ::testing::Test {
 protected:
  void Build(bool solid_outside_level = false) {
    tileset_ = MakeTileset(MakeTerrain(solid_outside_level));
    absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset_);
    ASSERT_TRUE(index.ok()) << index.status();
    index_ = std::move(*index);
    terrain_ = &tileset_.terrains[0];
  }

  void Paint(int x, int y) { ASSERT_TRUE(PaintTerrain(level_, index_, kTerrainId, x, y).ok()); }

  Tileset tileset_;
  TerrainIndex index_;
  const Terrain* terrain_ = nullptr;
  Level level_ = MakeLevel();
};

TEST_F(TerrainBrushTest, IsolatedCellHasEmptyMask) {
  Build();
  Paint(4, 4);
  EXPECT_EQ(MaskAt(level_, 4, 4), 0);
}

TEST_F(TerrainBrushTest, PaintingAdjacentCellRewritesTheExistingNeighbour) {
  Build();
  Paint(4, 4);
  ASSERT_EQ(MaskAt(level_, 4, 4), 0);

  Paint(5, 4);

  // The original cell now has an eastern neighbour, and vice versa.
  EXPECT_EQ(MaskAt(level_, 4, 4), kEast);
  EXPECT_EQ(MaskAt(level_, 5, 4), kWest);
}

TEST_F(TerrainBrushTest, FilledBlockResolvesCentreToFullySurrounded) {
  Build();
  for (int y = 3; y <= 5; ++y) {
    for (int x = 3; x <= 5; ++x) Paint(x, y);
  }

  EXPECT_EQ(MaskAt(level_, 4, 4), 255) << "centre of a 3x3 block is fully surrounded";
}

TEST_F(TerrainBrushTest, FilledBlockResolvesCornersAndEdges) {
  Build();
  for (int y = 3; y <= 5; ++y) {
    for (int x = 3; x <= 5; ++x) Paint(x, y);
  }

  // Top-left corner sees only east, south, and the south-east diagonal.
  EXPECT_EQ(MaskAt(level_, 3, 3), NormalizeNeighborMask(kEast | kSouth | kSouthEast));
  // Top edge additionally sees west and south-west.
  EXPECT_EQ(MaskAt(level_, 4, 3),
            NormalizeNeighborMask(kEast | kWest | kSouth | kSouthEast | kSouthWest));
  // Bottom-right corner mirrors the top-left.
  EXPECT_EQ(MaskAt(level_, 5, 5), NormalizeNeighborMask(kWest | kNorth | kNorthWest));
}

// The concave corner is the whole reason for choosing blob-47 over a 3x3 block:
// an L-shaped region produces masks a 9-tile set cannot express.
TEST_F(TerrainBrushTest, LShapedRegionProducesAnInnerCorner) {
  Build();
  // An L meeting at (4,4): its north and west arms are filled, but the corner
  // cell diagonally between them is not.
  //
  //   . X        (4,3) painted
  //   X X        (3,4) and (4,4) painted, (3,3) left empty
  Paint(4, 3);
  Paint(3, 4);
  Paint(4, 4);

  const uint8_t mask = MaskAt(level_, 4, 4);
  EXPECT_EQ(mask, NormalizeNeighborMask(kNorth | kWest));
  EXPECT_EQ(QuadrantStateForMask(mask, Quadrant::kNorthWest), QuadrantState::kInnerCorner)
      << "the concave corner a 3x3 tile block cannot express";

  // Filling the diagonal turns that same quadrant into plain interior.
  Paint(3, 3);
  EXPECT_EQ(MaskAt(level_, 4, 4), NormalizeNeighborMask(kNorth | kWest | kNorthWest));
  EXPECT_EQ(QuadrantStateForMask(MaskAt(level_, 4, 4), Quadrant::kNorthWest),
            QuadrantState::kFill);
}

TEST_F(TerrainBrushTest, DiagonalOnlyNeighbourIsNormalizedAway) {
  Build();
  Paint(3, 3);
  Paint(4, 4);

  // The two cells touch only at a corner, which normalization discards.
  EXPECT_EQ(MaskAt(level_, 4, 4), 0);
  EXPECT_EQ(MaskAt(level_, 3, 3), 0);
}

// --- Level bounds ------------------------------------------------------------

TEST_F(TerrainBrushTest, OutsideLevelCountsAsEmptyByDefault) {
  Build(/*solid_outside_level=*/false);
  Paint(0, 0);
  EXPECT_EQ(MaskAt(level_, 0, 0), 0);
}

TEST_F(TerrainBrushTest, SolidOutsideLevelKeepsTheBorderContinuous) {
  Build(/*solid_outside_level=*/true);
  Paint(0, 0);

  // North, west and their diagonal all lie outside the level and count as solid.
  EXPECT_EQ(MaskAt(level_, 0, 0), NormalizeNeighborMask(kNorth | kWest | kNorthWest));
}

// --- Erase -------------------------------------------------------------------

TEST_F(TerrainBrushTest, EraseClearsTheCellAndReresolvesSurvivors) {
  Build();
  Paint(4, 4);
  Paint(5, 4);
  ASSERT_EQ(MaskAt(level_, 4, 4), kEast);

  ASSERT_TRUE(EraseTerrain(level_, index_, 5, 4).ok());

  EXPECT_EQ(GetTileAt(level_, 5, 4).value(), 0);
  EXPECT_EQ(MaskAt(level_, 4, 4), 0) << "survivor lost its eastern neighbour";
}

TEST_F(TerrainBrushTest, EraseOnEmptyCellIsHarmless) {
  Build();
  ASSERT_TRUE(EraseTerrain(level_, index_, 7, 7).ok());
  EXPECT_EQ(GetTileAt(level_, 7, 7).value(), 0);
}

// --- Variants ----------------------------------------------------------------

TEST(TerrainVariantTest, SelectionIsStableAcrossRepeatedCalls) {
  TerrainRule rule{.mask = 255,
                   .variants = {{.tile_id = 10, .weight = 1},
                                {.tile_id = 11, .weight = 1},
                                {.tile_id = 12, .weight = 1}}};

  absl::StatusOr<int> first = SelectVariant(rule, 3, 9, kTerrainId);
  ASSERT_TRUE(first.ok()) << first.status();
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(SelectVariant(rule, 3, 9, kTerrainId).value(), *first);
  }
}

TEST(TerrainVariantTest, SelectionSpreadsAcrossVariants) {
  TerrainRule rule{.mask = 255,
                   .variants = {{.tile_id = 10, .weight = 1},
                                {.tile_id = 11, .weight = 1},
                                {.tile_id = 12, .weight = 1}}};

  std::set<int> chosen;
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) chosen.insert(SelectVariant(rule, x, y, kTerrainId).value());
  }
  EXPECT_EQ(chosen.size(), 3u) << "every variant should appear across a 16x16 area";
}

TEST(TerrainVariantTest, SingleVariantAlwaysWins) {
  TerrainRule rule{.mask = 0, .variants = {{.tile_id = 42, .weight = 1}}};
  EXPECT_EQ(SelectVariant(rule, 1, 1, kTerrainId).value(), 42);
  EXPECT_EQ(SelectVariant(rule, 99, 4, kTerrainId).value(), 42);
}

TEST(TerrainVariantTest, WeightsBiasSelection) {
  TerrainRule rule{.mask = 255,
                   .variants = {{.tile_id = 10, .weight = 9}, {.tile_id = 11, .weight = 1}}};

  int heavy = 0;
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 32; ++x) {
      if (SelectVariant(rule, x, y, kTerrainId).value() == 10) ++heavy;
    }
  }
  // Far from exact, but a 9:1 weighting must clearly favour the heavy variant.
  EXPECT_GT(heavy, 700);
  EXPECT_LT(heavy, 1024);
}

TEST(TerrainVariantTest, RejectsEmptyAndWeightlessRules) {
  EXPECT_FALSE(SelectVariant(TerrainRule{.mask = 0}, 0, 0, kTerrainId).ok());

  TerrainRule zero_weight{.mask = 0, .variants = {{.tile_id = 1, .weight = 0}}};
  EXPECT_FALSE(SelectVariant(zero_weight, 0, 0, kTerrainId).ok());
}

// Repainting an already-painted region must not reshuffle its artwork.
TEST_F(TerrainBrushTest, RepaintingARegionIsIdempotent) {
  Build();
  for (int y = 2; y <= 6; ++y) {
    for (int x = 2; x <= 6; ++x) Paint(x, y);
  }

  std::vector<int> before;
  for (int y = 2; y <= 6; ++y) {
    for (int x = 2; x <= 6; ++x) before.push_back(GetTileAt(level_, x, y).value());
  }

  for (int y = 2; y <= 6; ++y) {
    for (int x = 2; x <= 6; ++x) Paint(x, y);
  }

  std::vector<int> after;
  for (int y = 2; y <= 6; ++y) {
    for (int x = 2; x <= 6; ++x) after.push_back(GetTileAt(level_, x, y).value());
  }
  EXPECT_EQ(before, after);
}

// --- Membership: hand-placed pieces count as terrain --------------------------

// A slope tile is not something the brush writes, but painted ground must flow
// into it instead of capping off with an edge. This is the seam fix.
class TerrainMemberTest : public ::testing::Test {
 protected:
  // Tile ID kSlopeTileId stands in for a hand-placed slope unit.
  static constexpr int kSlopeTileId = 900;

  void SetUp() override {
    tileset_ = MakeTileset(MakeTerrain());
    tileset_.tiles.push_back(Tile{.id = kSlopeTileId,
                                  .name = "Slope45Up",
                                  .shape = TileShape::kSlope45BottomLeft});
    tileset_.terrains[0].member_tile_ids = {kSlopeTileId};

    absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset_);
    ASSERT_TRUE(index.ok()) << index.status();
    index_ = std::move(*index);
  }

  void Paint(int x, int y) { ASSERT_TRUE(PaintTerrain(level_, index_, kTerrainId, x, y).ok()); }

  Tileset tileset_;
  TerrainIndex index_;
  Level level_ = MakeLevel();
};

TEST_F(TerrainMemberTest, MemberTileResolvesToItsTerrain) {
  EXPECT_EQ(index_.FindByTileId(kSlopeTileId), index_.FindById(kTerrainId));
  // But it is not something the brush produces.
  EXPECT_EQ(index_.FindPaintableByTileId(kSlopeTileId), nullptr);
  EXPECT_EQ(index_.FindPaintableByTileId(kFirstTileId), index_.FindById(kTerrainId));
}

TEST_F(TerrainMemberTest, GroundPaintedBesideASlopeHasNoEdge) {
  // Place a slope by hand to the east, then paint ground beside it.
  ASSERT_TRUE(SetTileAt(level_, 5, 4, kSlopeTileId).ok());
  Paint(4, 4);

  // The east bit is set: the slope reads as the same material, not as air.
  EXPECT_EQ(MaskAt(level_, 4, 4), kEast);
}

TEST_F(TerrainMemberTest, WithoutMembershipTheSameSlopeWouldReadAsAir) {
  // Same geometry, but the slope is not a member of the terrain.
  Tileset without_members = tileset_;
  without_members.terrains[0].member_tile_ids.clear();
  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(without_members);
  ASSERT_TRUE(index.ok()) << index.status();

  ASSERT_TRUE(SetTileAt(level_, 5, 4, kSlopeTileId).ok());
  ASSERT_TRUE(PaintTerrain(level_, *index, kTerrainId, 4, 4).ok());

  EXPECT_EQ(MaskAt(level_, 4, 4), 0) << "this is the seam the membership fix removes";
}

TEST_F(TerrainMemberTest, PaintingBesideASlopeNeverOverwritesIt) {
  ASSERT_TRUE(SetTileAt(level_, 5, 4, kSlopeTileId).ok());

  // Paint all around the slope, repeatedly.
  for (int repeat = 0; repeat < 2; ++repeat) {
    Paint(4, 4);
    Paint(5, 3);
    Paint(5, 5);
    Paint(6, 4);
  }

  EXPECT_EQ(GetTileAt(level_, 5, 4).value(), kSlopeTileId)
      << "refresh must never re-resolve a member tile";
}

TEST_F(TerrainMemberTest, ErasingASlopeReresolvesSurvivingGround) {
  ASSERT_TRUE(SetTileAt(level_, 5, 4, kSlopeTileId).ok());
  Paint(4, 4);
  ASSERT_EQ(MaskAt(level_, 4, 4), kEast);

  ASSERT_TRUE(EraseTerrain(level_, index_, 5, 4).ok());

  EXPECT_EQ(GetTileAt(level_, 5, 4).value(), 0);
  EXPECT_EQ(MaskAt(level_, 4, 4), 0) << "ground should grow an edge where the slope was";
}

TEST_F(TerrainMemberTest, PaintingDirectlyOnASlopeReplacesItDeliberately) {
  ASSERT_TRUE(SetTileAt(level_, 4, 4, kSlopeTileId).ok());
  Paint(4, 4);

  EXPECT_NE(GetTileAt(level_, 4, 4).value(), kSlopeTileId)
      << "an explicit paint on the cell is a deliberate replacement";
}

TEST(TerrainIndexTest, RejectsTileThatIsBothPaintedAndAMember) {
  Tileset tileset = MakeTileset(MakeTerrain());
  tileset.terrains[0].member_tile_ids = {kFirstTileId};

  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset);
  ASSERT_FALSE(index.ok());
  EXPECT_EQ(index.status().code(), absl::StatusCode::kInvalidArgument);
}

// --- Failure modes -----------------------------------------------------------

TEST_F(TerrainBrushTest, PaintingUnknownTerrainFails) {
  Build();
  absl::Status status = PaintTerrain(level_, index_, 999, 1, 1);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST_F(TerrainBrushTest, PaintingNegativeCoordinatesFails) {
  Build();
  EXPECT_FALSE(PaintTerrain(level_, index_, kTerrainId, -1, 0).ok());
}

// A hand-edited terrain missing a mask must fail loudly rather than paint the
// wrong artwork.
TEST_F(TerrainBrushTest, MissingRuleFailsInsteadOfGuessing) {
  tileset_ = MakeTileset(MakeTerrain());
  // Drop the rule for the isolated-cell mask.
  std::vector<TerrainRule>& rules = tileset_.terrains[0].rules;
  rules.erase(rules.begin());

  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset_);
  ASSERT_TRUE(index.ok()) << index.status();

  absl::Status status = PaintTerrain(level_, *index, kTerrainId, 4, 4);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

}  // namespace
}  // namespace zebes
