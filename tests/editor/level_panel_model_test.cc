#include "editor/level_editor/level_panel_model.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace zebes {
namespace {

// A level carrying placed tiles. The chunk key is arbitrary: what matters is
// that non-zero IDs exist somewhere, which is what freezes the tileset binding.
Level LevelWithTiles(std::string tileset_id, int count) {
  Level level{.id = "alpha", .tileset_id = std::move(tileset_id)};
  TileChunk chunk{};
  for (int i = 0; i < count; ++i) chunk.tiles[i] = i + 1;
  level.tile_chunks[0] = chunk;
  return level;
}

TEST(LevelPanelModelTest, OrderedCatalogPreservesDuplicateNames) {
  LevelPanelModel model;
  model.SetLevels({{.id = "b", .name = "Cave"}, {.id = "a", .name = "Cave"}});

  ASSERT_EQ(model.levels().size(), 2);
  EXPECT_EQ(model.levels().begin()->first.id, "a");
  EXPECT_EQ(model.levels().begin()->second.id, "a");
}

TEST(LevelPanelModelTest, SelectionUsesStableIdAcrossRefresh) {
  LevelPanelModel model;
  model.SetLevels({{.id = "west", .name = "Z West"}, {.id = "east", .name = "A East"}});
  ASSERT_TRUE(model.SelectLevel("west").ok());

  model.SetLevels({{.id = "west", .name = "A West"}, {.id = "east", .name = "Z East"}});

  EXPECT_EQ(model.selected_level_id(), "west");
  ASSERT_TRUE(model.BeginEditingSelectedLevel().ok());
  ASSERT_NE(model.active_level(), nullptr);
  EXPECT_EQ(model.active_level()->id, "west");
}

TEST(LevelPanelModelTest, EditingUsesCopyInsteadOfCatalogStorage) {
  LevelPanelModel model;
  model.SetLevels({{.id = "cave", .name = "Cave"}});
  ASSERT_TRUE(model.SelectLevel("cave").ok());
  ASSERT_TRUE(model.BeginEditingSelectedLevel().ok());

  model.active_level()->name = "Changed";

  EXPECT_EQ(model.levels().begin()->second.name, "Cave");
}

TEST(LevelPanelModelTest, CreateFinishesAsExistingLevel) {
  LevelPanelModel model;
  model.BeginNewLevel();

  ASSERT_TRUE(model.is_new_level());
  EXPECT_EQ(model.active_level()->name, "name");
  ASSERT_TRUE(model.FinishCreate("generated").ok());
  EXPECT_FALSE(model.is_new_level());
  EXPECT_EQ(model.active_level()->id, "generated");
  EXPECT_EQ(model.selected_level_id(), "generated");
  EXPECT_EQ(model.FinishCreate("duplicate").code(), absl::StatusCode::kFailedPrecondition);
}

TEST(LevelPanelModelTest, InvalidSelectionDoesNotOpenLevel) {
  LevelPanelModel model;
  model.SetLevels({{.id = "cave", .name = "Cave"}});

  EXPECT_EQ(model.SelectLevel("missing").code(), absl::StatusCode::kNotFound);
  EXPECT_EQ(model.BeginEditingSelectedLevel().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_FALSE(model.has_active_level());
}

TEST(LevelPanelModelTest, DeleteClearsSelectionAndActiveLevel) {
  LevelPanelModel model;
  model.SetLevels({{.id = "cave", .name = "Cave"}});
  ASSERT_TRUE(model.SelectLevel("cave").ok());
  ASSERT_TRUE(model.BeginEditingSelectedLevel().ok());

  model.FinishDelete();

  EXPECT_FALSE(model.has_level_selection());
  EXPECT_FALSE(model.has_active_level());
  EXPECT_EQ(model.BuildSaveRequest().status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(LevelPanelModelTilesetTest, ActiveTilesetNameFallsBackToTheRawId) {
  LevelPanelModel model;
  model.BeginEditingLevel(Level{.id = "alpha", .tileset_id = "missing-uuid"});

  EXPECT_EQ(model.ActiveTilesetName(), "missing-uuid");

  model.SetTilesetChoices({{.id = "missing-uuid", .name = "Grass"}});
  EXPECT_EQ(model.ActiveTilesetName(), "Grass");
}

TEST(LevelPanelModelTilesetTest, AnUnboundLevelHasNoTilesetName) {
  LevelPanelModel model;
  model.BeginEditingLevel(Level{.id = "alpha"});

  EXPECT_EQ(model.ActiveTilesetName(), "");
}

TEST(LevelPanelModelTilesetTest, ChangingTilesetNeedsAnActiveLevel) {
  LevelPanelModel model;

  EXPECT_EQ(model.RequestTilesetChange("grass").code(), absl::StatusCode::kFailedPrecondition);
}

TEST(LevelPanelModelTilesetTest, AnEmptyLevelRebindsWithoutConfirmation) {
  LevelPanelModel model;
  model.BeginEditingLevel(Level{.id = "alpha", .tileset_id = "sunny"});

  ASSERT_TRUE(model.RequestTilesetChange("grass").ok());

  EXPECT_FALSE(model.has_pending_tileset_change());
  EXPECT_EQ(model.active_level()->tileset_id, "grass");
}

// Reselecting what the level already uses must not stage anything, or a level
// with tiles would offer to discard them for no change at all.
TEST(LevelPanelModelTilesetTest, ReselectingTheCurrentTilesetIsANoOp) {
  LevelPanelModel model;
  model.BeginEditingLevel(LevelWithTiles("sunny", 1));

  ASSERT_TRUE(model.RequestTilesetChange("sunny").ok());

  EXPECT_FALSE(model.has_pending_tileset_change());
  EXPECT_EQ(model.placed_tile_count(), 1);
}

TEST(LevelPanelModelTilesetTest, APopulatedLevelStagesTheChangeAndKeepsItsTiles) {
  LevelPanelModel model;
  model.BeginEditingLevel(LevelWithTiles("sunny", 2));

  ASSERT_TRUE(model.RequestTilesetChange("grass").ok());

  EXPECT_TRUE(model.has_pending_tileset_change());
  EXPECT_EQ(model.pending_tileset_id(), "grass");
  EXPECT_EQ(model.placed_tile_count(), 2);
  // Nothing changes until it is confirmed.
  EXPECT_EQ(model.active_level()->tileset_id, "sunny");
}

TEST(LevelPanelModelTilesetTest, ConfirmingDiscardsTilesAndRebinds) {
  LevelPanelModel model;
  model.BeginEditingLevel(LevelWithTiles("sunny", 1));
  ASSERT_TRUE(model.RequestTilesetChange("grass").ok());

  ASSERT_TRUE(model.ConfirmTilesetChange().ok());

  EXPECT_EQ(model.active_level()->tileset_id, "grass");
  EXPECT_EQ(model.placed_tile_count(), 0);
  EXPECT_FALSE(model.has_pending_tileset_change());
}

TEST(LevelPanelModelTilesetTest, CancellingKeepsBothTilesetAndTiles) {
  LevelPanelModel model;
  model.BeginEditingLevel(LevelWithTiles("sunny", 1));
  ASSERT_TRUE(model.RequestTilesetChange("grass").ok());

  model.CancelTilesetChange();

  EXPECT_FALSE(model.has_pending_tileset_change());
  EXPECT_EQ(model.active_level()->tileset_id, "sunny");
  EXPECT_EQ(model.placed_tile_count(), 1);
}

TEST(LevelPanelModelTilesetTest, ConfirmingWithNothingStagedFails) {
  LevelPanelModel model;
  model.BeginEditingLevel(Level{.id = "alpha", .tileset_id = "sunny"});

  EXPECT_EQ(model.ConfirmTilesetChange().code(), absl::StatusCode::kFailedPrecondition);
}

// A staged change belongs to the level that was open when it was requested.
TEST(LevelPanelModelTilesetTest, ClosingTheLevelDropsAStagedChange) {
  LevelPanelModel model;
  model.BeginEditingLevel(LevelWithTiles("sunny", 1));
  ASSERT_TRUE(model.RequestTilesetChange("grass").ok());

  model.CloseActiveLevel();

  EXPECT_FALSE(model.has_pending_tileset_change());
}

}  // namespace
}  // namespace zebes
