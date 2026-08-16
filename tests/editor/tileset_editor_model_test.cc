#include "editor/tileset_editor/tileset_editor_model.h"

#include <limits>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/texture.h"
#include "objects/tileset.h"
#include "terrain/blob47_compose.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

TEST(TilesetEditorModelTest, OrderedCatalogPreservesDuplicateNames) {
  TilesetEditorModel model;
  model.SetTilesets({{.id = "b", .name = "Cavern"}, {.id = "a", .name = "Cavern"}});

  ASSERT_EQ(model.tilesets().size(), 2);
  EXPECT_EQ(model.tilesets().begin()->first.display_name, "Cavern");
  EXPECT_EQ(model.tilesets().begin()->first.id, "a");
  EXPECT_EQ(model.tilesets().begin()->second.id, "a");
}

TEST(TilesetEditorModelTest, SelectionUsesStableIdsAcrossCatalogRefresh) {
  TilesetEditorModel model;
  model.SetTilesets({{.id = "forest", .name = "Forest"}, {.id = "cave", .name = "Cave"}});
  ASSERT_TRUE(model.SelectTileset("forest").ok());

  model.SetTilesets({{.id = "cave", .name = "A Cave"}, {.id = "forest", .name = "Z Forest"}});

  EXPECT_EQ(model.selected_tileset_id(), "forest");
  ASSERT_TRUE(model.BeginEditingSelectedTileset().ok());
  EXPECT_EQ(model.active_tileset()->id, "forest");
}

TEST(TilesetEditorModelTest, NewAndExistingSaveTransitionsAreExplicit) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  ASSERT_TRUE(model.has_active_tileset());
  EXPECT_TRUE(model.is_new_tileset());
  ASSERT_TRUE(model.BuildSaveRequest().ok());

  ASSERT_TRUE(model.FinishSave("generated-id").ok());
  EXPECT_FALSE(model.is_new_tileset());
  EXPECT_EQ(model.selected_tileset_id(), "generated-id");

  model.CloseActiveTileset();
  EXPECT_FALSE(model.has_active_tileset());
  EXPECT_EQ(model.BuildSaveRequest().status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(TilesetEditorModelTest, TextureSelectionResolvesActiveTexture) {
  TilesetEditorModel model;
  model.SetTextures({{.id = "lava", .name = "Lava"}, {.id = "ice", .name = "Ice"}});
  model.BeginNewTileset();

  ASSERT_TRUE(model.SelectTexture("ice").ok());
  ASSERT_NE(model.active_texture(), nullptr);
  EXPECT_EQ(model.active_texture()->id, "ice");
  EXPECT_EQ(model.SelectTexture("missing").code(), absl::StatusCode::kNotFound);
}

TEST(TilesetEditorModelTest, TileOperationsUseStableIds) {
  TilesetEditorModel model;
  model.SetTilesets({{.id = "tileset",
                      .name = "Tileset",
                      .tiles = {{.id = 8, .name = "Eight"}, {.id = 3, .name = "Three"}}}});
  ASSERT_TRUE(model.SelectTileset("tileset").ok());
  ASSERT_TRUE(model.BeginEditingSelectedTileset().ok());

  ASSERT_TRUE(model.SelectTile(3).ok());
  ASSERT_NE(model.selected_tile(), nullptr);
  EXPECT_EQ(model.selected_tile()->name, "Three");

  ASSERT_TRUE(model.AddTile().ok());
  EXPECT_EQ(model.selected_tile_id(), 9);
  ASSERT_NE(model.selected_tile(), nullptr);
  EXPECT_EQ(model.selected_tile()->id, 9);

  ASSERT_TRUE(model.DeleteSelectedTile().ok());
  EXPECT_EQ(model.selected_tile_id(), 0);
  EXPECT_EQ(model.active_tileset()->tiles.size(), 2);
}

TEST(TilesetEditorModelTest, AtlasCellsSnapUsingBothTileDimensions) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  model.active_tileset()->tile_width = 16;
  model.active_tileset()->tile_height = 8;
  ASSERT_TRUE(model.AddTile().ok());

  absl::StatusOr<AtlasCell> cell = model.CalculateAtlasCell(31.9, 17.0, 64, 32);
  ASSERT_TRUE(cell.ok());
  EXPECT_EQ(cell->source_x, 16);
  EXPECT_EQ(cell->source_y, 16);
  ASSERT_TRUE(model.SetSelectedTileSource(*cell).ok());
  EXPECT_EQ(model.selected_tile()->source_x, 16);
  EXPECT_EQ(model.selected_tile()->source_y, 16);

  EXPECT_EQ(model.CalculateAtlasCell(64.0, 0.0, 64, 32).status().code(),
            absl::StatusCode::kOutOfRange);
  EXPECT_EQ(model.CalculateAtlasCell(-0.1, 0.0, 64, 32).status().code(),
            absl::StatusCode::kOutOfRange);
}

TEST(TilesetEditorModelTest, TileIdExhaustionReturnsError) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  model.active_tileset()->tiles.push_back(
      Tile{.id = std::numeric_limits<int>::max(), .name = "Last"});

  EXPECT_EQ(model.AddTile().code(), absl::StatusCode::kResourceExhausted);
}

// --- Terrain authoring -------------------------------------------------------

namespace {

// A minimal but valid quadrant sheet, composed into a manifest the way the
// compose_blob47 tool emits one.
std::string MakeManifest(int variant_count) {
  QuadrantSheet sheet;
  sheet.quadrant_size = 8;
  sheet.variant_count = variant_count;
  sheet.image.width = sheet.quadrant_size * kQuadrantStateCount * variant_count;
  sheet.image.height = sheet.quadrant_size * kQuadrantCount;
  sheet.image.pixels.assign(static_cast<size_t>(sheet.image.width) * sheet.image.height * 4, 255);

  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(sheet);
  EXPECT_TRUE(atlas.ok()) << atlas.status();
  return WriteBlob47Manifest(*atlas);
}

}  // namespace

TEST(TilesetEditorModelTest, ImportTerrainAddsTilesAndOneTerrain) {
  TilesetEditorModel model;
  model.BeginNewTileset();

  ASSERT_TRUE(model.ImportTerrainManifest(MakeManifest(1)).ok());

  EXPECT_EQ(model.active_tileset()->tiles.size(), kBlob47TileCount);
  ASSERT_EQ(model.active_tileset()->terrains.size(), 1u);
  EXPECT_EQ(model.active_tileset()->terrains[0].rules.size(), kBlob47TileCount);
}

// Importing must not renumber tiles that already exist in the tileset.
TEST(TilesetEditorModelTest, ImportTerrainPreservesExistingTileIds) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  model.active_tileset()->tiles.push_back(Tile{.id = 4, .name = "Existing"});

  ASSERT_TRUE(model.ImportTerrainManifest(MakeManifest(1)).ok());

  EXPECT_EQ(model.active_tileset()->tiles.front().id, 4);
  EXPECT_EQ(model.active_tileset()->tiles.front().name, "Existing");
  // Imported tiles start after the highest existing ID.
  EXPECT_EQ(model.active_tileset()->tiles[1].id, 5);
}

TEST(TilesetEditorModelTest, ImportTerrainTwiceProducesDistinctTerrainIds) {
  TilesetEditorModel model;
  model.BeginNewTileset();

  ASSERT_TRUE(model.ImportTerrainManifest(MakeManifest(1)).ok());
  ASSERT_TRUE(model.ImportTerrainManifest(MakeManifest(1)).ok());

  ASSERT_EQ(model.active_tileset()->terrains.size(), 2u);
  EXPECT_NE(model.active_tileset()->terrains[0].id, model.active_tileset()->terrains[1].id);
  EXPECT_EQ(model.active_tileset()->tiles.size(), kBlob47TileCount * 2);
}

TEST(TilesetEditorModelTest, ImportTerrainGroupsVariantsUnderOneRule) {
  TilesetEditorModel model;
  model.BeginNewTileset();

  ASSERT_TRUE(model.ImportTerrainManifest(MakeManifest(2)).ok());

  ASSERT_EQ(model.active_tileset()->terrains.size(), 1u);
  for (const TerrainRule& rule : model.active_tileset()->terrains[0].rules) {
    EXPECT_EQ(rule.variants.size(), 2u);
  }
}

TEST(TilesetEditorModelTest, ImportTerrainRejectsMalformedManifest) {
  TilesetEditorModel model;
  model.BeginNewTileset();

  EXPECT_EQ(model.ImportTerrainManifest("{ nope").code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(model.active_tileset()->terrains.empty());
}

TEST(TilesetEditorModelTest, ImportTerrainRequiresAnActiveTileset) {
  TilesetEditorModel model;
  EXPECT_EQ(model.ImportTerrainManifest(MakeManifest(1)).code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(TilesetEditorModelTest, DetectTerrainsFindsAnImportedBlock) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  model.active_tileset()->tile_width = 16;
  model.active_tileset()->tile_height = 16;
  ASSERT_TRUE(model.ImportTerrainManifest(MakeManifest(1)).ok());
  // Drop the imported terrain so detection has to rediscover it from the tiles.
  model.active_tileset()->terrains.clear();

  absl::StatusOr<int> added = model.DetectTerrains();
  ASSERT_TRUE(added.ok()) << added.status();
  EXPECT_EQ(*added, 1);
  EXPECT_EQ(model.active_tileset()->terrains.size(), 1u);
}

TEST(TilesetEditorModelTest, DetectTerrainsFindsNothingInAHandAuthoredTileset) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  for (int i = 1; i <= 9; ++i) {
    model.active_tileset()->tiles.push_back(
        Tile{.id = i, .name = "Grass", .source_x = (i % 3) * 16, .source_y = (i / 3) * 16});
  }

  absl::StatusOr<int> added = model.DetectTerrains();
  ASSERT_TRUE(added.ok()) << added.status();
  EXPECT_EQ(*added, 0);
  EXPECT_TRUE(model.active_tileset()->terrains.empty());
}

TEST(TilesetEditorModelTest, TerrainMembershipAssignsAndClears) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  ASSERT_TRUE(model.ImportTerrainManifest(MakeManifest(1)).ok());
  const int terrain_id = model.active_tileset()->terrains[0].id;

  // A hand-drawn slope that the brush never paints.
  model.active_tileset()->tiles.push_back(
      Tile{.id = 900, .name = "Slope", .shape = TileShape::kSlope45FloorTallRight});

  EXPECT_FALSE(model.GetTileTerrainMembership(900).has_value());
  ASSERT_TRUE(model.SetTileTerrainMembership(900, terrain_id).ok());
  EXPECT_EQ(model.GetTileTerrainMembership(900), terrain_id);
  EXPECT_THAT(model.active_tileset()->terrains[0].shape_tile_ids, ::testing::ElementsAre(900));

  ASSERT_TRUE(model.SetTileTerrainMembership(900, std::nullopt).ok());
  EXPECT_FALSE(model.GetTileTerrainMembership(900).has_value());
  EXPECT_TRUE(model.active_tileset()->terrains[0].shape_tile_ids.empty());
}

// Membership is exclusive: assigning to a second terrain must not leave the
// tile listed under the first, which TerrainIndex would reject.
TEST(TilesetEditorModelTest, TerrainMembershipIsExclusive) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  ASSERT_TRUE(model.ImportTerrainManifest(MakeManifest(1)).ok());
  ASSERT_TRUE(model.ImportTerrainManifest(MakeManifest(1)).ok());
  const int first = model.active_tileset()->terrains[0].id;
  const int second = model.active_tileset()->terrains[1].id;
  model.active_tileset()->tiles.push_back(Tile{.id = 900, .name = "Slope"});

  ASSERT_TRUE(model.SetTileTerrainMembership(900, first).ok());
  ASSERT_TRUE(model.SetTileTerrainMembership(900, second).ok());

  EXPECT_TRUE(model.active_tileset()->terrains[0].shape_tile_ids.empty());
  EXPECT_THAT(model.active_tileset()->terrains[1].shape_tile_ids, ::testing::ElementsAre(900));
  EXPECT_EQ(model.GetTileTerrainMembership(900), second);
}

TEST(TilesetEditorModelTest, TerrainMembershipRejectsAPaintedTile) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  ASSERT_TRUE(model.ImportTerrainManifest(MakeManifest(1)).ok());
  const int terrain_id = model.active_tileset()->terrains[0].id;
  const int painted_tile_id = model.active_tileset()->tiles[0].id;

  absl::Status status = model.SetTileTerrainMembership(painted_tile_id, terrain_id);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(TilesetEditorModelTest, TerrainMembershipRejectsUnknownTerrain) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  model.active_tileset()->tiles.push_back(Tile{.id = 900, .name = "Slope"});

  EXPECT_EQ(model.SetTileTerrainMembership(900, 42).code(), absl::StatusCode::kNotFound);
}

TEST(TilesetEditorModelTest, DeleteTerrainRemovesOnlyThatTerrain) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  ASSERT_TRUE(model.ImportTerrainManifest(MakeManifest(1)).ok());
  ASSERT_TRUE(model.ImportTerrainManifest(MakeManifest(1)).ok());
  const int removed_id = model.active_tileset()->terrains[0].id;
  const int kept_id = model.active_tileset()->terrains[1].id;

  ASSERT_TRUE(model.DeleteTerrain(removed_id).ok());

  ASSERT_EQ(model.active_tileset()->terrains.size(), 1u);
  EXPECT_EQ(model.active_tileset()->terrains[0].id, kept_id);

  // Deleting an absent terrain is a no-op, not an error.
  ASSERT_TRUE(model.DeleteTerrain(removed_id).ok());
  EXPECT_EQ(model.active_tileset()->terrains.size(), 1u);
}

// Every terrain atlas the generator writes is 32px. A tileset that slices the
// wrong grid looks fine in the navigator and wrong only once tiles render.
TEST(TilesetEditorModelTest, NewTilesetsUseTheSizeTerrainArtIsAuthoredAt) {
  TilesetEditorModel model;
  model.BeginNewTileset();

  ASSERT_NE(model.active_tileset(), nullptr);
  EXPECT_EQ(model.active_tileset()->tile_width, 32);
  EXPECT_EQ(model.active_tileset()->tile_height, 32);
}

TEST(TilesetEditorModelTest, ANewlyOpenedTilesetHasNothingToLose) {
  TilesetEditorModel model;
  EXPECT_FALSE(model.has_unsaved_changes()) << "nothing is being edited";

  model.SetTilesets({{.id = "grass", .name = "Grass", .tile_width = 32, .tile_height = 32}});
  ASSERT_TRUE(model.SelectTileset("grass").ok());
  ASSERT_TRUE(model.BeginEditingSelectedTileset().ok());

  EXPECT_FALSE(model.has_unsaved_changes());
}

TEST(TilesetEditorModelTest, EditsToAnyPartOfATilesetCount) {
  TilesetEditorModel model;
  model.SetTilesets({{.id = "grass", .name = "Grass", .tile_width = 32, .tile_height = 32}});
  ASSERT_TRUE(model.SelectTileset("grass").ok());

  ASSERT_TRUE(model.BeginEditingSelectedTileset().ok());
  model.active_tileset()->name = "Renamed";
  EXPECT_TRUE(model.has_unsaved_changes());

  ASSERT_TRUE(model.BeginEditingSelectedTileset().ok());
  ASSERT_TRUE(model.AddTile().ok());
  EXPECT_TRUE(model.has_unsaved_changes());

  ASSERT_TRUE(model.BeginEditingSelectedTileset().ok());
  model.active_tileset()->terrains.push_back(Terrain{.id = 1, .name = "Dirt"});
  EXPECT_TRUE(model.has_unsaved_changes());
}

// Comparing against a snapshot rather than latching a flag means undoing an
// edit by hand correctly reports clean again.
TEST(TilesetEditorModelTest, RestoringAnEditedFieldIsCleanAgain) {
  TilesetEditorModel model;
  model.SetTilesets({{.id = "grass", .name = "Grass", .tile_width = 32, .tile_height = 32}});
  ASSERT_TRUE(model.SelectTileset("grass").ok());
  ASSERT_TRUE(model.BeginEditingSelectedTileset().ok());

  model.active_tileset()->name = "Renamed";
  ASSERT_TRUE(model.has_unsaved_changes());

  model.active_tileset()->name = "Grass";
  EXPECT_FALSE(model.has_unsaved_changes());
}

TEST(TilesetEditorModelTest, SavingMakesTheCurrentStateTheCleanOne) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  model.active_tileset()->name = "Cavern";
  ASSERT_TRUE(model.has_unsaved_changes());

  ASSERT_TRUE(model.FinishSave("cavern-id").ok());
  EXPECT_FALSE(model.has_unsaved_changes());

  model.active_tileset()->tile_width = 16;
  EXPECT_TRUE(model.has_unsaved_changes());
}

// Cutting an atlas by hand costs roughly four interactions per tile, so a
// forty-cell sheet was not worth doing. One drag replaces the first two.
TEST(TilesetEditorModelTest, ARegionAddsOneTilePerCell) {
  TilesetEditorModel model;
  model.BeginNewTileset();

  absl::StatusOr<int> added =
      model.AddTilesForRegion({.source_x = 0, .source_y = 0}, {.source_x = 64, .source_y = 32});
  ASSERT_TRUE(added.ok()) << added.status();
  EXPECT_EQ(*added, 6);

  const std::vector<Tile>& tiles = model.active_tileset()->tiles;
  ASSERT_EQ(tiles.size(), 6u);

  // Row-major across the region, numbered from the next free ID.
  EXPECT_EQ(tiles[0].id, 1);
  EXPECT_EQ(tiles[0].source_x, 0);
  EXPECT_EQ(tiles[0].source_y, 0);
  EXPECT_EQ(tiles[2].source_x, 64);
  EXPECT_EQ(tiles[2].source_y, 0);
  EXPECT_EQ(tiles[3].source_x, 0);
  EXPECT_EQ(tiles[3].source_y, 32);
  EXPECT_EQ(tiles[5].id, 6);
  EXPECT_EQ(tiles[5].source_x, 64);
  EXPECT_EQ(tiles[5].source_y, 32);
}

// A drag runs in whatever direction the user moved, so neither corner leads.
TEST(TilesetEditorModelTest, RegionCornersMayBeGivenInAnyOrder) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  ASSERT_TRUE(
      model.AddTilesForRegion({.source_x = 64, .source_y = 32}, {.source_x = 0, .source_y = 0})
          .ok());

  std::vector<std::pair<int, int>> sources;
  for (const Tile& tile : model.active_tileset()->tiles) {
    sources.push_back({tile.source_x, tile.source_y});
  }
  EXPECT_THAT(sources, ::testing::UnorderedElementsAre(std::pair(0, 0), std::pair(32, 0),
                                                       std::pair(64, 0), std::pair(0, 32),
                                                       std::pair(32, 32), std::pair(64, 32)));
}

// Dragging back over work already done should add what is missing, not a second
// tile pointing at the same artwork.
TEST(TilesetEditorModelTest, ARegionSkipsCellsThatAlreadyHaveATile) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  ASSERT_TRUE(model.AddTile().ok());
  model.selected_tile()->source_x = 32;
  model.selected_tile()->source_y = 0;

  absl::StatusOr<int> added =
      model.AddTilesForRegion({.source_x = 0, .source_y = 0}, {.source_x = 64, .source_y = 0});
  ASSERT_TRUE(added.ok()) << added.status();
  EXPECT_EQ(*added, 2);
  EXPECT_EQ(model.active_tileset()->tiles.size(), 3u);

  // Dragging the same region again adds nothing at all.
  absl::StatusOr<int> again =
      model.AddTilesForRegion({.source_x = 0, .source_y = 0}, {.source_x = 64, .source_y = 0});
  ASSERT_TRUE(again.ok()) << again.status();
  EXPECT_EQ(*again, 0);
}

// kNone means no collision at all. Handing someone a screenful of silently
// non-colliding tiles is the worse default; a wrong shape is at least visible
// in the overlay.
TEST(TilesetEditorModelTest, BulkAddedTilesAreSolidByDefault) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  ASSERT_TRUE(
      model.AddTilesForRegion({.source_x = 0, .source_y = 0}, {.source_x = 32, .source_y = 0})
          .ok());

  for (const Tile& tile : model.active_tileset()->tiles) {
    EXPECT_EQ(tile.shape, TileShape::kFullBlock);
  }
}

TEST(TilesetEditorModelTest, ARegionSelectsTheLastTileItAdded) {
  TilesetEditorModel model;
  model.BeginNewTileset();
  ASSERT_TRUE(
      model.AddTilesForRegion({.source_x = 0, .source_y = 0}, {.source_x = 32, .source_y = 32})
          .ok());

  ASSERT_NE(model.selected_tile(), nullptr);
  EXPECT_EQ(model.selected_tile()->source_x, 32);
  EXPECT_EQ(model.selected_tile()->source_y, 32);
}

TEST(TilesetEditorModelTest, ARegionOffTheCellGridIsRejected) {
  TilesetEditorModel model;
  model.BeginNewTileset();

  absl::StatusOr<int> added =
      model.AddTilesForRegion({.source_x = 5, .source_y = 0}, {.source_x = 32, .source_y = 0});
  EXPECT_EQ(added.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(model.active_tileset()->tiles.empty());
}

TEST(TilesetEditorModelTest, ARegionNeedsATilesetToAddTo) {
  TilesetEditorModel model;

  absl::StatusOr<int> added =
      model.AddTilesForRegion({.source_x = 0, .source_y = 0}, {.source_x = 0, .source_y = 0});
  EXPECT_EQ(added.status().code(), absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
