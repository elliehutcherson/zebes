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
  EXPECT_EQ(model.active_tileset()->tile_width, 16);
  EXPECT_EQ(model.active_tileset()->tile_height, 16);
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
  sheet.image.pixels.assign(
      static_cast<size_t>(sheet.image.width) * sheet.image.height * 4, 255);

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
      Tile{.id = 900, .name = "Slope", .shape = TileShape::kSlope45BottomLeft});

  EXPECT_FALSE(model.GetTileTerrainMembership(900).has_value());
  ASSERT_TRUE(model.SetTileTerrainMembership(900, terrain_id).ok());
  EXPECT_EQ(model.GetTileTerrainMembership(900), terrain_id);
  EXPECT_THAT(model.active_tileset()->terrains[0].member_tile_ids, ::testing::ElementsAre(900));

  ASSERT_TRUE(model.SetTileTerrainMembership(900, std::nullopt).ok());
  EXPECT_FALSE(model.GetTileTerrainMembership(900).has_value());
  EXPECT_TRUE(model.active_tileset()->terrains[0].member_tile_ids.empty());
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

  EXPECT_TRUE(model.active_tileset()->terrains[0].member_tile_ids.empty());
  EXPECT_THAT(model.active_tileset()->terrains[1].member_tile_ids, ::testing::ElementsAre(900));
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

}  // namespace
}  // namespace zebes
