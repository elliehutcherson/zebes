#include "editor/level_editor/level_panel_model.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

// A level carrying placed tiles. The chunk key is arbitrary: what matters is
// that non-zero IDs exist somewhere, which is what freezes the tileset binding.
Level LevelWithTiles(std::string tileset_id, int count) {
  Level level{.id = "alpha", .tileset_id = std::move(tileset_id)};
  TileChunk chunk{};
  for (int i = 0; i < count; ++i) chunk.tiles[i] = i + 1;
  level.layers.front().tile_chunks[0] = chunk;
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
  ASSERT_OK(model.SelectLevel("west"));

  model.SetLevels({{.id = "west", .name = "A West"}, {.id = "east", .name = "Z East"}});

  EXPECT_EQ(model.selected_level_id(), "west");
  ASSERT_OK(model.BeginEditingSelectedLevel());
  ASSERT_NE(model.active_level(), nullptr);
  EXPECT_EQ(model.active_level()->id, "west");
}

TEST(LevelPanelModelTest, EditingUsesCopyInsteadOfCatalogStorage) {
  LevelPanelModel model;
  model.SetLevels({{.id = "cave", .name = "Cave"}});
  ASSERT_OK(model.SelectLevel("cave"));
  ASSERT_OK(model.BeginEditingSelectedLevel());

  model.active_level()->name = "Changed";

  EXPECT_EQ(model.levels().begin()->second.name, "Cave");
}

TEST(LevelPanelModelTest, CreateFinishesAsExistingLevel) {
  LevelPanelModel model;
  model.BeginNewLevel();

  ASSERT_TRUE(model.is_new_level());
  EXPECT_EQ(model.active_level()->name, "name");
  ASSERT_OK(model.FinishCreate("generated"));
  EXPECT_FALSE(model.is_new_level());
  EXPECT_EQ(model.active_level()->id, "generated");
  EXPECT_EQ(model.selected_level_id(), "generated");
  EXPECT_EQ(model.FinishCreate("duplicate").code(), absl::StatusCode::kFailedPrecondition);
}

TEST(LevelPanelModelTest, InvalidSelectionDoesNotOpenLevel) {
  LevelPanelModel model;
  model.SetLevels({{.id = "cave", .name = "Cave"}});

  EXPECT_EQ(model.SelectLevel("missing").code(), absl::StatusCode::kNotFound);
  EXPECT_EQ(model.BeginEditingSelectedLevel().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_FALSE(model.has_active_level());
}

TEST(LevelPanelModelTest, DeleteClearsSelectionAndActiveLevel) {
  LevelPanelModel model;
  model.SetLevels({{.id = "cave", .name = "Cave"}});
  ASSERT_OK(model.SelectLevel("cave"));
  ASSERT_OK(model.BeginEditingSelectedLevel());

  model.FinishDelete();

  EXPECT_FALSE(model.has_level_selection());
  EXPECT_FALSE(model.has_active_level());
  EXPECT_EQ(model.BuildSaveRequest().status().code(), absl::StatusCode::kFailedPrecondition);
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

  ASSERT_OK(model.RequestTilesetChange("grass"));

  EXPECT_FALSE(model.has_pending_tileset_change());
  EXPECT_EQ(model.active_level()->tileset_id, "grass");
}

// Reselecting what the level already uses must not stage anything, or a level
// with tiles would offer to discard them for no change at all.
TEST(LevelPanelModelTilesetTest, ReselectingTheCurrentTilesetIsANoOp) {
  LevelPanelModel model;
  model.BeginEditingLevel(LevelWithTiles("sunny", 1));

  ASSERT_OK(model.RequestTilesetChange("sunny"));

  EXPECT_FALSE(model.has_pending_tileset_change());
  EXPECT_EQ(model.placed_tile_count(), 1);
}

TEST(LevelPanelModelTilesetTest, APopulatedLevelStagesTheChangeAndKeepsItsTiles) {
  LevelPanelModel model;
  model.BeginEditingLevel(LevelWithTiles("sunny", 2));

  ASSERT_OK(model.RequestTilesetChange("grass"));

  EXPECT_TRUE(model.has_pending_tileset_change());
  EXPECT_EQ(model.pending_tileset_id(), "grass");
  EXPECT_EQ(model.placed_tile_count(), 2);
  // Nothing changes until it is confirmed.
  EXPECT_EQ(model.active_level()->tileset_id, "sunny");
}

TEST(LevelPanelModelTilesetTest, ConfirmingDiscardsTilesAndRebinds) {
  LevelPanelModel model;
  model.BeginEditingLevel(LevelWithTiles("sunny", 1));
  ASSERT_OK(model.RequestTilesetChange("grass"));

  ASSERT_OK(model.ConfirmTilesetChange());

  EXPECT_EQ(model.active_level()->tileset_id, "grass");
  EXPECT_EQ(model.placed_tile_count(), 0);
  EXPECT_FALSE(model.has_pending_tileset_change());
}

TEST(LevelPanelModelTilesetTest, CancellingKeepsBothTilesetAndTiles) {
  LevelPanelModel model;
  model.BeginEditingLevel(LevelWithTiles("sunny", 1));
  ASSERT_OK(model.RequestTilesetChange("grass"));

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
  ASSERT_OK(model.RequestTilesetChange("grass"));

  model.CloseActiveLevel();

  EXPECT_FALSE(model.has_pending_tileset_change());
}

// Back used to discard edits with no prompt. Knowing whether there is anything
// to lose is what lets it only ask when asking is warranted.
TEST(LevelPanelModelTest, AFreshlyOpenedLevelHasNothingToLose) {
  LevelPanelModel model;
  EXPECT_FALSE(model.has_unsaved_changes()) << "nothing is being edited";

  model.SetLevels({Level{.id = "a", .name = "Alpha"}});
  ASSERT_OK(model.SelectLevel("a"));
  ASSERT_OK(model.BeginEditingSelectedLevel());

  EXPECT_FALSE(model.has_unsaved_changes());
}

TEST(LevelPanelModelTest, EditsToAnyPartOfALevelCount) {
  LevelPanelModel model;
  model.SetLevels({Level{.id = "a", .name = "Alpha"}});
  ASSERT_OK(model.SelectLevel("a"));

  ASSERT_OK(model.BeginEditingSelectedLevel());
  model.active_level()->name = "Renamed";
  EXPECT_TRUE(model.has_unsaved_changes());

  // Painting is the edit that matters most and the one a flag is most likely
  // to miss, since it goes straight into the chunk map.
  ASSERT_OK(model.BeginEditingSelectedLevel());
  model.active_level()->layers.front().tile_chunks[0].tiles[5] = 7;
  EXPECT_TRUE(model.has_unsaved_changes());

  ASSERT_OK(model.BeginEditingSelectedLevel());
  model.active_level()->layers.front().entities[1] = Entity{.id = 1};
  EXPECT_TRUE(model.has_unsaved_changes());
}

// Comparing against a snapshot rather than latching a flag means painting a
// tile and erasing it again correctly reports clean.
TEST(LevelPanelModelTest, UndoingAnEditByHandIsCleanAgain) {
  LevelPanelModel model;
  model.SetLevels({Level{.id = "a", .name = "Alpha"}});
  ASSERT_OK(model.SelectLevel("a"));
  ASSERT_OK(model.BeginEditingSelectedLevel());

  model.active_level()->layers.front().tile_chunks[0].tiles[5] = 7;
  ASSERT_TRUE(model.has_unsaved_changes());

  model.active_level()->layers.front().tile_chunks.erase(0);
  EXPECT_FALSE(model.has_unsaved_changes());
}

TEST(LevelPanelModelTest, SavingMakesTheCurrentStateTheCleanOne) {
  LevelPanelModel model;
  model.BeginNewLevel();
  model.active_level()->name = "Cavern";
  ASSERT_TRUE(model.has_unsaved_changes());

  ASSERT_OK(model.FinishCreate("cavern-id"));
  EXPECT_FALSE(model.has_unsaved_changes());

  model.active_level()->name = "Renamed";
  ASSERT_TRUE(model.has_unsaved_changes());

  model.MarkSaved();
  EXPECT_FALSE(model.has_unsaved_changes());
}

}  // namespace
}  // namespace zebes
