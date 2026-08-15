#include "editor/level_editor/terrain_brush.h"

#include <set>

#include "editor/level_editor/viewport_model.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

constexpr int kTerrainId = 5;
constexpr int kTileSize = 16;
// Stands in for a hand-placed slope unit in the key tests below.
constexpr int kSlopeNeighbourTileId = 900;

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
    // Blob tiles are full blocks, as BuildTerrainCandidate emits them. The
    // brush reads a cell's geometry back off its tile when refreshing, so a
    // fixture leaving this at kNone would describe a terrain of holes.
    tileset.tiles.push_back(
        Tile{.id = kFirstTileId + i, .name = "T", .shape = TileShape::kFullBlock});
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
    provider_.emplace(index_);
  }

  void Paint(int x, int y) {
    ASSERT_TRUE(
        PaintTerrain(level_, index_, *provider_, kTerrainId, TileShape::kFullBlock, x, y).ok());
  }

  Tileset tileset_;
  TerrainIndex index_;
  std::optional<Blob47TileProvider> provider_;
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
  EXPECT_EQ(QuadrantStateForMask(MaskAt(level_, 4, 4), Quadrant::kNorthWest), QuadrantState::kFill);
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

  ASSERT_TRUE(EraseTerrain(level_, index_, *provider_, 5, 4).ok());

  EXPECT_EQ(GetTileAt(level_, 5, 4).value(), 0);
  EXPECT_EQ(MaskAt(level_, 4, 4), 0) << "survivor lost its eastern neighbour";
}

TEST_F(TerrainBrushTest, EraseOnEmptyCellIsHarmless) {
  Build();
  ASSERT_TRUE(EraseTerrain(level_, index_, *provider_, 7, 7).ok());
  EXPECT_EQ(GetTileAt(level_, 7, 7).value(), 0);
}

// --- Variants ----------------------------------------------------------------

// Variants with no imposed phase: the brush is free to pick per cell.
Terrain UnstructuredTerrain() {
  return Terrain{.id = kTerrainId, .name = "Test", .variant_period = 0};
}

TEST(TerrainVariantTest, SelectionIsStableAcrossRepeatedCalls) {
  const Terrain terrain = UnstructuredTerrain();
  TerrainRule rule{.mask = 255,
                   .variants = {{.tile_id = 10, .weight = 1},
                                {.tile_id = 11, .weight = 1},
                                {.tile_id = 12, .weight = 1}}};

  absl::StatusOr<int> first = SelectVariant(terrain, rule, 3, 9);
  ASSERT_TRUE(first.ok()) << first.status();
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(SelectVariant(terrain, rule, 3, 9).value(), *first);
  }
}

TEST(TerrainVariantTest, SelectionSpreadsAcrossVariants) {
  const Terrain terrain = UnstructuredTerrain();
  TerrainRule rule{.mask = 255,
                   .variants = {{.tile_id = 10, .weight = 1},
                                {.tile_id = 11, .weight = 1},
                                {.tile_id = 12, .weight = 1}}};

  std::set<int> chosen;
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) chosen.insert(SelectVariant(terrain, rule, x, y).value());
  }
  EXPECT_EQ(chosen.size(), 3u) << "every variant should appear across a 16x16 area";
}

TEST(TerrainVariantTest, SingleVariantAlwaysWins) {
  const Terrain terrain = UnstructuredTerrain();
  TerrainRule rule{.mask = 0, .variants = {{.tile_id = 42, .weight = 1}}};
  EXPECT_EQ(SelectVariant(terrain, rule, 1, 1).value(), 42);
  EXPECT_EQ(SelectVariant(terrain, rule, 99, 4).value(), 42);
}

TEST(TerrainVariantTest, WeightsBiasSelection) {
  const Terrain terrain = UnstructuredTerrain();
  TerrainRule rule{.mask = 255,
                   .variants = {{.tile_id = 10, .weight = 9}, {.tile_id = 11, .weight = 1}}};

  int heavy = 0;
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 32; ++x) {
      if (SelectVariant(terrain, rule, x, y).value() == 10) ++heavy;
    }
  }
  // Far from exact, but a 9:1 weighting must clearly favour the heavy variant.
  EXPECT_GT(heavy, 700);
  EXPECT_LT(heavy, 1024);
}

TEST(TerrainVariantTest, RejectsEmptyAndWeightlessRules) {
  const Terrain terrain = UnstructuredTerrain();
  EXPECT_FALSE(SelectVariant(terrain, TerrainRule{.mask = 0}, 0, 0).ok());

  TerrainRule zero_weight{.mask = 0, .variants = {{.tile_id = 1, .weight = 0}}};
  EXPECT_FALSE(SelectVariant(terrain, zero_weight, 0, 0).ok());
}

// --- Periodic variants -------------------------------------------------------

// A terrain whose four variants are the phases of a 2x2 pattern.
Terrain PeriodicTerrain() {
  return Terrain{.id = kTerrainId, .name = "Periodic", .variant_period = 2};
}

TerrainRule PeriodicRule() {
  return TerrainRule{.mask = 255,
                     .variants = {{.tile_id = 10, .weight = 1},
                                  {.tile_id = 11, .weight = 1},
                                  {.tile_id = 12, .weight = 1},
                                  {.tile_id = 13, .weight = 1}}};
}

// The phase has to follow the cell's position, or the pattern tears at every
// tile border -- which is the whole reason the mode exists.
TEST(TerrainVariantTest, PeriodicSelectionFollowsCellPosition) {
  const Terrain terrain = PeriodicTerrain();
  const TerrainRule rule = PeriodicRule();

  EXPECT_EQ(SelectVariant(terrain, rule, 0, 0).value(), 10);
  EXPECT_EQ(SelectVariant(terrain, rule, 1, 0).value(), 11);
  EXPECT_EQ(SelectVariant(terrain, rule, 0, 1).value(), 12);
  EXPECT_EQ(SelectVariant(terrain, rule, 1, 1).value(), 13);

  // And it repeats, so a wide run lays the same pattern down over and over.
  EXPECT_EQ(SelectVariant(terrain, rule, 2, 2).value(), 10);
  EXPECT_EQ(SelectVariant(terrain, rule, 7, 4).value(), 11);
}

// Levels extend into negative coordinates, where a naive modulo returns a
// negative phase and indexes off the front of the variant list.
TEST(TerrainVariantTest, PeriodicSelectionHandlesNegativeCoordinates) {
  const Terrain terrain = PeriodicTerrain();
  const TerrainRule rule = PeriodicRule();

  EXPECT_EQ(SelectVariant(terrain, rule, -2, -2).value(), 10);
  EXPECT_EQ(SelectVariant(terrain, rule, -1, -2).value(), 11);
  EXPECT_EQ(SelectVariant(terrain, rule, -2, -1).value(), 12);
  EXPECT_EQ(SelectVariant(terrain, rule, -1, -1).value(), 13);
}

TEST(TerrainVariantTest, PeriodicSelectionIgnoresWeights) {
  const Terrain terrain = PeriodicTerrain();
  TerrainRule rule = PeriodicRule();
  rule.variants[0].weight = 99;

  EXPECT_EQ(SelectVariant(terrain, rule, 1, 1).value(), 13)
      << "a phase is a position, not a preference";
}

TEST(TerrainVariantTest, PeriodicSelectionRejectsAMissingPhase) {
  const Terrain terrain = PeriodicTerrain();
  TerrainRule rule = PeriodicRule();
  rule.variants.pop_back();

  EXPECT_FALSE(SelectVariant(terrain, rule, 0, 0).ok())
      << "three variants cannot cover a 2x2 pattern";
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

// --- The cell key -------------------------------------------------------------

TEST_F(TerrainBrushTest, TheKeyRecordsWhatANeighbourIsNotMerelyThatItIsThere) {
  // The whole reason the key replaced a bare mask. A wedge and a full block
  // both set the same bit, so a mask cannot tell the renderer which is beside
  // it, and the artwork was baked against a guess.
  Build();
  Paint(4, 4);
  ASSERT_TRUE(SetTileAt(level_, 5, 4, kSlopeNeighbourTileId).ok());

  Tileset with_slope = tileset_;
  with_slope.tiles.push_back(
      Tile{.id = kSlopeNeighbourTileId, .name = "Slope", .shape = TileShape::kSlope45BottomLeft});
  with_slope.terrains[0].shape_tile_ids = {kSlopeNeighbourTileId};
  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(with_slope);
  ASSERT_TRUE(index.ok()) << index.status();

  absl::StatusOr<TerrainCellKey> key =
      ComputeTerrainCellKey(level_, *index, with_slope.terrains[0], TileShape::kFullBlock, 4, 4);
  ASSERT_TRUE(key.ok()) << key.status();

  EXPECT_EQ(key->neighbors[2], TileShape::kSlope45BottomLeft) << "east is the slope";
  EXPECT_EQ(key->neighbors[6], TileShape::kNone) << "west is air";
  // Projected down to a mask, the slope becomes indistinguishable from ground.
  EXPECT_EQ(NeighborMaskOf(*key), kEast);
}

TEST_F(TerrainBrushTest, ACellOutsideTheLevelReadsAsAFullBlockWhenGroundIsContinuous) {
  Build(/*solid_outside_level=*/true);

  absl::StatusOr<TerrainCellKey> key =
      ComputeTerrainCellKey(level_, index_, *terrain_, TileShape::kFullBlock, 0, 0);
  ASSERT_TRUE(key.ok()) << key.status();

  EXPECT_EQ(key->neighbors[6], TileShape::kFullBlock) << "west is outside the level";
  EXPECT_EQ(key->neighbors[0], TileShape::kFullBlock) << "north is outside the level";
}

TEST_F(TerrainBrushTest, ThePhaseFollowsWhereTheCellSitsInTheRepeat) {
  Build();
  tileset_.terrains[0].variant_period = 2;
  const Terrain& periodic = tileset_.terrains[0];

  const auto phase_at = [&](int x, int y) {
    absl::StatusOr<TerrainCellKey> key =
        ComputeTerrainCellKey(level_, index_, periodic, TileShape::kFullBlock, x, y);
    EXPECT_TRUE(key.ok()) << key.status();
    return key.ok() ? key->phase : -1;
  };

  EXPECT_EQ(phase_at(0, 0), 0);
  EXPECT_EQ(phase_at(1, 0), 1);
  EXPECT_EQ(phase_at(0, 1), 2);
  EXPECT_EQ(phase_at(1, 1), 3);
  EXPECT_EQ(phase_at(2, 2), 0) << "the pattern repeats";
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
    tileset_.tiles.push_back(
        Tile{.id = kSlopeTileId, .name = "Slope45Up", .shape = TileShape::kSlope45BottomLeft});
    tileset_.terrains[0].shape_tile_ids = {kSlopeTileId};

    absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset_);
    ASSERT_TRUE(index.ok()) << index.status();
    index_ = std::move(*index);
    provider_.emplace(index_);
  }

  void Paint(int x, int y) {
    ASSERT_TRUE(
        PaintTerrain(level_, index_, *provider_, kTerrainId, TileShape::kFullBlock, x, y).ok());
  }

  Tileset tileset_;
  TerrainIndex index_;
  std::optional<Blob47TileProvider> provider_;
  Level level_ = MakeLevel();
};

TEST_F(TerrainMemberTest, MemberTileResolvesToItsTerrain) {
  EXPECT_EQ(index_.FindByTileId(kSlopeTileId), index_.FindById(kTerrainId));
  EXPECT_EQ(index_.FindByTileId(kFirstTileId), index_.FindById(kTerrainId));
  // The index used to answer a second question -- may the brush write this
  // tile -- so a refresh could not replace a hand-placed slope with a blob
  // tile. A refresh now hands a cell back the shape it already had, so
  // re-resolving that slope returns the same slope and the question has no
  // remaining user. PaintingBesideASlopeNeverOverwritesIt is what holds it.
  EXPECT_EQ(index_.ShapeOfTile(kSlopeTileId), TileShape::kSlope45BottomLeft);
  EXPECT_EQ(index_.ShapeOfTile(kFirstTileId), TileShape::kFullBlock);
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
  without_members.terrains[0].shape_tile_ids.clear();
  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(without_members);
  ASSERT_TRUE(index.ok()) << index.status();
  // The provider must read the index this test built, not the fixture's.
  Blob47TileProvider provider(*index);

  ASSERT_TRUE(SetTileAt(level_, 5, 4, kSlopeTileId).ok());
  ASSERT_TRUE(PaintTerrain(level_, *index, provider, kTerrainId, TileShape::kFullBlock, 4, 4).ok());

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

  ASSERT_TRUE(EraseTerrain(level_, index_, *provider_, 5, 4).ok());

  EXPECT_EQ(GetTileAt(level_, 5, 4).value(), 0);
  EXPECT_EQ(MaskAt(level_, 4, 4), 0) << "ground should grow an edge where the slope was";
}

TEST_F(TerrainMemberTest, PaintingDirectlyOnASlopeReplacesItDeliberately) {
  ASSERT_TRUE(SetTileAt(level_, 4, 4, kSlopeTileId).ok());
  Paint(4, 4);

  EXPECT_NE(GetTileAt(level_, 4, 4).value(), kSlopeTileId)
      << "an explicit paint on the cell is a deliberate replacement";
}

TEST(TerrainIndexTest, ATileListedTwiceByOneTerrainIsHarmless) {
  // This used to be an error: a tile could be rule-produced or hand-placed but
  // not both, because the two answered different questions about whether the
  // brush might rewrite it. The brush now hands every cell back its own shape,
  // so there is one question left -- which terrain owns this tile -- and being
  // named twice by the same terrain answers it the same way both times.
  Tileset tileset = MakeTileset(MakeTerrain());
  tileset.terrains[0].shape_tile_ids = {kFirstTileId};

  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset);

  ASSERT_OK(index);
  EXPECT_EQ(index->FindByTileId(kFirstTileId), index->FindById(kTerrainId));
}

TEST(TerrainIndexTest, RejectsATileTwoTerrainsBothClaim) {
  // Still an error, and the one that mattered: a painted cell's neighbourhood
  // would be ambiguous about which material it holds.
  Tileset tileset = MakeTileset(MakeTerrain());
  Terrain other = MakeTerrain();
  other.id = kTerrainId + 1;
  other.name = "Stone";
  other.rules.clear();
  other.shape_tile_ids = {kFirstTileId};
  tileset.terrains.push_back(std::move(other));

  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset);

  ASSERT_FALSE(index.ok());
  EXPECT_EQ(index.status().code(), absl::StatusCode::kInvalidArgument);
}

// --- Failure modes -----------------------------------------------------------

TEST_F(TerrainBrushTest, PaintingUnknownTerrainFails) {
  Build();
  absl::Status status = PaintTerrain(level_, index_, *provider_, 999, TileShape::kFullBlock, 1, 1);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST_F(TerrainBrushTest, PaintingNegativeCoordinatesFails) {
  Build();
  EXPECT_FALSE(
      PaintTerrain(level_, index_, *provider_, kTerrainId, TileShape::kFullBlock, -1, 0).ok());
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
  // The provider must read the index this test built, not the fixture's.
  Blob47TileProvider provider(*index);

  absl::Status status =
      PaintTerrain(level_, *index, provider, kTerrainId, TileShape::kFullBlock, 4, 4);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

// --- The tile provider --------------------------------------------------------

TEST_F(TerrainMemberTest, TheProviderResolvesAShapeThroughTheTerrainsShapeTiles) {
  // A blob-47 terrain keys its painted artwork on a mask, but a slope is not a
  // mask -- it resolves by the shape asked for.
  TerrainCellKey key;
  key.shape = TileShape::kSlope45BottomLeft;
  key.neighbors.fill(TileShape::kFullBlock);

  absl::StatusOr<int> tile = provider_->TileForKey(tileset_.terrains[0], key, 4, 4);

  ASSERT_TRUE(tile.ok()) << tile.status();
  EXPECT_EQ(*tile, kSlopeTileId);
}

TEST_F(TerrainMemberTest, AShapeTheTerrainHasNoArtworkForIsRefused) {
  // Fail loudly rather than substitute a block, which would put geometry in the
  // level that the player collides with but nobody chose.
  TerrainCellKey key;
  key.shape = TileShape::kSteepSlopeTopRightTop;
  key.neighbors.fill(TileShape::kFullBlock);

  absl::StatusOr<int> tile = provider_->TileForKey(tileset_.terrains[0], key, 4, 4);

  ASSERT_FALSE(tile.ok());
  EXPECT_EQ(tile.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(TerrainMemberTest, ARefreshHandsACellBackTheGeometryItAlreadyHad) {
  // The invariant the whole phase rests on: a refresh may change how a cell
  // looks, never what the player collides with. RefreshNeighbors reads each
  // neighbour's shape and passes it straight back, so there is no path that
  // chooses geometry on the author's behalf.
  ASSERT_TRUE(SetTileAt(level_, 5, 4, kSlopeTileId).ok());

  Paint(4, 4);
  Paint(5, 5);
  Paint(6, 4);

  ASSERT_EQ(GetTileAt(level_, 5, 4).value(), kSlopeTileId);
  EXPECT_EQ(index_.ShapeOfTile(GetTileAt(level_, 5, 4).value()), TileShape::kSlope45BottomLeft);
}

TEST(TerrainIndexTest, RejectsTwoTilesClaimingOneShape) {
  // Which tile a slope cell resolves to would otherwise depend on the order of
  // shape_tile_ids.
  Tileset tileset = MakeTileset(MakeTerrain());
  tileset.tiles.push_back(
      Tile{.id = 901, .name = "SlopeA", .shape = TileShape::kSlope45BottomLeft});
  tileset.tiles.push_back(
      Tile{.id = 902, .name = "SlopeB", .shape = TileShape::kSlope45BottomLeft});
  tileset.terrains[0].shape_tile_ids = {901, 902};

  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset);

  ASSERT_FALSE(index.ok());
  EXPECT_EQ(index.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
