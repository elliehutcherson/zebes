#include "editor/tileset_editor/tileset_editor_model.h"

#include <limits>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "objects/texture.h"
#include "objects/tileset.h"

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

}  // namespace
}  // namespace zebes
