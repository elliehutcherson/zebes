#include <string>
#include <vector>

#include "common/status_macros.h"
#include "editor/level_editor/derived_tile_provider.h"
#include "editor/level_editor/terrain_brush.h"
#include "editor/level_editor/viewport_model.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "terrain/terrain_content_index.h"

namespace zebes {
namespace {

// The invariant the whole derived-artwork design exists to hold:
//
//   the artwork a painted cell ends up referencing is exactly the artwork the
//   renderer produces for that cell's real neighbourhood.
//
// This replaces a file that measured how far the baked atlas fell short of that
// -- a ramp meeting open air was 172 of 1024 pixels wrong, ground beside a slope
// 50 to 115 -- because the atlas no longer guesses, so there is nothing left to
// measure. The equality is asserted instead.
//
// It is not tautological. The provider renders from a key, but the key is
// computed from the level by ComputeTerrainCellKey: which neighbours count as
// this terrain, what shape each one holds, what phase the cell sits at. Getting
// any of that wrong produces artwork for a neighbourhood the level does not
// have, and this is what would notice.

constexpr int kTileSize = 16;
constexpr int kTerrainId = 4;

TerrainGenConfig RecipeConfig() {
  TerrainGenConfig config;
  config.tile_size = kTileSize;
  config.supersample = 1;
  config.variant_period = 1;
  config.seed = 20260814;
  return config;
}

TileShape ShapeFromChar(char c) {
  switch (c) {
    case '#':
      return TileShape::kFullBlock;
    case '/':
      return TileShape::kSlope45FloorTallRight;
    case '\\':
      return TileShape::kSlope45FloorTallLeft;
    case 'a':
      return TileShape::kGentleSlopeFloorTallRightLower;
    case 'b':
      return TileShape::kGentleSlopeFloorTallRightUpper;
    case 'h':
      return TileShape::kHalfBlockBottom;
    default:
      return TileShape::kNone;
  }
}

Tileset DerivedTileset() {
  Tileset tileset;
  tileset.id = "derived";
  tileset.name = "Cave";
  tileset.texture_id = "tx";
  tileset.tile_width = kTileSize;
  tileset.tile_height = kTileSize;

  Terrain terrain;
  terrain.id = kTerrainId;
  terrain.name = "Cave";
  terrain.scheme = TerrainScheme::kDerived;
  terrain.solid_outside_level = false;
  terrain.variant_period = 1;
  tileset.terrains.push_back(std::move(terrain));
  return tileset;
}

RgbaImage BlankAtlas() {
  RgbaImage atlas;
  atlas.width = 8 * kTileSize;
  atlas.height = kTileSize;
  atlas.pixels.assign(static_cast<size_t>(atlas.width) * atlas.height * 4, 0);
  return atlas;
}

Level MakeLevel(int tiles_wide, int tiles_high) {
  Level level;
  level.tile_render_width = kTileSize;
  level.tile_render_height = kTileSize;
  level.width = tiles_wide * kTileSize;
  level.height = tiles_high * kTileSize;
  return level;
}

class DerivedArtworkTest : public ::testing::Test {
 protected:
  // Paints a scene one cell at a time, rebuilding the terrain index after each
  // cell because painting appends tiles that the next cell's neighbours have to
  // be able to recognise -- which is what the editor does every frame.
  void PaintScene(const std::vector<std::string>& rows) {
    rows_ = rows;
    level_ = MakeLevel(static_cast<int>(rows[0].size()), static_cast<int>(rows.size()));
    tileset_ = DerivedTileset();

    ASSERT_OK_AND_ASSIGN(TerrainRenderer renderer, TerrainRenderer::Create(RecipeConfig()));
    renderer_ = std::make_unique<TerrainRenderer>(std::move(renderer));

    ASSERT_OK_AND_ASSIGN(TerrainRenderer for_provider, TerrainRenderer::Create(RecipeConfig()));
    ASSERT_OK_AND_ASSIGN(
        DerivedTileProvider provider,
        DerivedTileProvider::Create(std::move(for_provider), tileset_, BlankAtlas()));
    provider_ = std::make_unique<DerivedTileProvider>(std::move(provider));

    for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
      for (int x = 0; x < static_cast<int>(rows[y].size()); ++x) {
        const TileShape shape = ShapeFromChar(rows[y][x]);
        if (shape == TileShape::kNone) continue;

        ASSERT_OK_AND_ASSIGN(TerrainIndex index, TerrainIndex::Build(tileset_));
        ASSERT_OK(PaintTerrain(level_, index, *provider_, kTerrainId, shape, x, y));
      }
    }
  }

  TileShape SceneShapeAt(int x, int y) const {
    if (y < 0 || y >= static_cast<int>(rows_.size())) return TileShape::kNone;
    if (x < 0 || x >= static_cast<int>(rows_[y].size())) return TileShape::kNone;
    return ShapeFromChar(rows_[y][x]);
  }

  // The artwork the level actually references for a cell.
  absl::StatusOr<RgbaImage> PaintedArtwork(int x, int y) {
    ASSIGN_OR_RETURN(const int tile_id, GetTileAt(level_, x, y));

    for (const Tile& tile : tileset_.tiles) {
      if (tile.id != tile_id) continue;
      return CropRegion(provider_->atlas(), tile.source_x, tile.source_y, kTileSize, kTileSize);
    }
    return absl::NotFoundError(
        absl::StrCat("cell (", x, ", ", y, ") references missing tile ", tile_id));
  }

  // The artwork the renderer produces for that cell's real neighbourhood,
  // derived from the scene rather than from anything the pipeline recorded.
  absl::StatusOr<RgbaImage> ExpectedArtwork(int x, int y) {
    std::vector<TileShape> neighbors(kNeighborCount, TileShape::kNone);
    for (int i = 0; i < kNeighborCount; ++i) {
      neighbors[i] = SceneShapeAt(x + kNeighborOffsets[i].dx, y + kNeighborOffsets[i].dy);
    }
    return renderer_->RenderShapeTileInContext(SceneShapeAt(x, y), neighbors, /*variant=*/0);
  }

  // Narrows a failure one step further: does the cell hold the tile the
  // provider would give for the scene's own key?
  void ExpectCellsHoldTheTileTheirKeyResolvesTo() {
    for (int y = 0; y < static_cast<int>(rows_.size()); ++y) {
      for (int x = 0; x < static_cast<int>(rows_[y].size()); ++x) {
        const TileShape shape = SceneShapeAt(x, y);
        if (shape == TileShape::kNone) continue;

        TerrainCellKey from_scene;
        from_scene.shape = shape;
        for (int i = 0; i < kNeighborCount; ++i) {
          from_scene.neighbors[i] =
              SceneShapeAt(x + kNeighborOffsets[i].dx, y + kNeighborOffsets[i].dy);
        }
        const absl::StatusOr<int> resolved =
            provider_->TileForKey(tileset_.terrains[0], from_scene, x, y);
        const absl::StatusOr<int> placed = GetTileAt(level_, x, y);
        ASSERT_OK(resolved);
        ASSERT_OK(placed);
        EXPECT_EQ(*placed, *resolved) << "cell (" << x << ", " << y << ")";
      }
    }
  }

  // Narrows a failure: is the key wrong, or the artwork chosen for it?
  void ExpectKeysMatchTheScene() {
    absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset_);
    ASSERT_OK(index);
    const Terrain* terrain = index->FindById(kTerrainId);
    ASSERT_NE(terrain, nullptr);

    for (int y = 0; y < static_cast<int>(rows_.size()); ++y) {
      for (int x = 0; x < static_cast<int>(rows_[y].size()); ++x) {
        const TileShape shape = SceneShapeAt(x, y);
        if (shape == TileShape::kNone) continue;

        absl::StatusOr<TerrainCellKey> key =
            ComputeTerrainCellKey(level_, *index, *terrain, shape, x, y);
        ASSERT_OK(key);

        TerrainCellKey from_scene;
        from_scene.shape = shape;
        for (int i = 0; i < kNeighborCount; ++i) {
          from_scene.neighbors[i] =
              SceneShapeAt(x + kNeighborOffsets[i].dx, y + kNeighborOffsets[i].dy);
        }
        EXPECT_EQ(*key, from_scene)
            << "cell (" << x << ", " << y << ")\n  level: " << DebugString(*key)
            << "\n  scene: " << DebugString(from_scene);
      }
    }
  }

  void ExpectEveryCellMatchesItsNeighbourhood() {
    for (int y = 0; y < static_cast<int>(rows_.size()); ++y) {
      for (int x = 0; x < static_cast<int>(rows_[y].size()); ++x) {
        if (SceneShapeAt(x, y) == TileShape::kNone) continue;

        const absl::StatusOr<RgbaImage> painted = PaintedArtwork(x, y);
        const absl::StatusOr<RgbaImage> expected = ExpectedArtwork(x, y);
        ASSERT_OK(painted);
        ASSERT_OK(expected);
        int different = 0;
        for (size_t i = 0; i < painted->pixels.size() && i < expected->pixels.size(); ++i) {
          if (painted->pixels[i] != expected->pixels[i]) ++different;
        }
        EXPECT_EQ(painted->pixels, expected->pixels)
            << "cell (" << x << ", " << y << ") holding "
            << kTileShapeIdentifiers[static_cast<size_t>(SceneShapeAt(x, y))]
            << " is not drawn for the neighbourhood it actually has; " << different << " of "
            << painted->pixels.size() << " bytes differ";
      }
    }
  }

  std::vector<std::string> rows_;
  Level level_;
  Tileset tileset_;
  std::unique_ptr<TerrainRenderer> renderer_;
  std::unique_ptr<DerivedTileProvider> provider_;
};

TEST_F(DerivedArtworkTest, FlatGroundIsDrawnForItsNeighbourhood) {
  PaintScene({"....", "####", "####"});

  ExpectKeysMatchTheScene();
  ExpectCellsHoldTheTileTheirKeyResolvesTo();
  ExpectEveryCellMatchesItsNeighbourhood();
}

TEST_F(DerivedArtworkTest, ARampEndingInAirIsDrawnForTheAirItEndsIn) {
  // The defect the baked atlas could not express at all: it filled the cell
  // beyond the ramp's uphill face with solid and drew buried interior against
  // open sky.
  PaintScene({"../.", "###."});

  ExpectEveryCellMatchesItsNeighbourhood();
}

TEST_F(DerivedArtworkTest, TwoRampsMeetingAtAPeakAreEachDrawnForTheOther) {
  PaintScene({"./\\.", "####"});

  ExpectEveryCellMatchesItsNeighbourhood();
}

TEST_F(DerivedArtworkTest, GroundBesideASlopeIsDrawnAgainstTheSlope) {
  // The defect no amount of slope artwork could fix, because the wrong tile was
  // a blob tile: its mask said "same terrain" and the band was measured against
  // a square. There is no mask in this path at all now.
  PaintScene({"#\\./#", "#####"});

  ExpectEveryCellMatchesItsNeighbourhood();
}

TEST_F(DerivedArtworkTest, ARampWithALandingIsContinuousThroughEveryPiece) {
  // Lower half, flat half blocks, upper half. Every piece meets its neighbour
  // at half tile height, and each is drawn knowing what it meets.
  PaintScene({".ahhb.", "######"});

  ExpectEveryCellMatchesItsNeighbourhood();
}

TEST_F(DerivedArtworkTest, TheAtlasAlsoHoldsNeighbourhoodsThatOnlyExistedMidStroke) {
  // Eight cells, and rather more than eight tiles. Painting is sequential, so a
  // cell is first drawn for a half-finished neighbourhood and redrawn as its
  // neighbours arrive; the earlier drawing stays in the atlas even once nothing
  // references it.
  //
  // That is the fragmentation the design accepts, and the reason compaction is
  // an explicit tool rather than something that happens on its own -- reclaiming
  // these would renumber tiles that levels already name. What matters is that
  // the count is bounded by what was painted rather than by combinatorics: a
  // key with eight neighbours could in principle reach thousands.
  PaintScene({"....", "####", "####"});

  EXPECT_GT(tileset_.tiles.size(), 8u) << "transient neighbourhoods leave tiles behind";
  EXPECT_LT(tileset_.tiles.size(), 32u) << "but only a small multiple of the cells painted";
}

}  // namespace
}  // namespace zebes
