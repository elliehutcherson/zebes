#include "editor/blueprint_editor/blueprint_panel_model.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

TEST(BlueprintPanelModelTest, OrderedCatalogPreservesDuplicateNames) {
  BlueprintPanelModel model;
  model.SetBlueprints({{.id = "b", .name = "Enemy"}, {.id = "a", .name = "Enemy"}});

  ASSERT_EQ(model.blueprints().size(), 2);
  EXPECT_EQ(model.blueprints().begin()->first.id, "a");
  EXPECT_EQ(model.blueprints().begin()->second.id, "a");
}

TEST(BlueprintPanelModelTest, SelectionUsesStableIdAcrossRefresh) {
  BlueprintPanelModel model;
  model.SetBlueprints({{.id = "player", .name = "Z Player"}, {.id = "enemy", .name = "A Enemy"}});
  ASSERT_OK(model.SelectBlueprint("player"));

  model.SetBlueprints({{.id = "player", .name = "A Player"}, {.id = "enemy", .name = "Z Enemy"}});

  EXPECT_EQ(model.selected_blueprint_id(), "player");
  ASSERT_OK(model.BeginEditingSelectedBlueprint());
  ASSERT_NE(model.active_blueprint(), nullptr);
  EXPECT_EQ(model.active_blueprint()->id, "player");
}

TEST(BlueprintPanelModelTest, EditingUsesCopyInsteadOfCatalogStorage) {
  BlueprintPanelModel model;
  model.SetBlueprints({{.id = "player", .name = "Player"}});
  ASSERT_OK(model.SelectBlueprint("player"));
  ASSERT_OK(model.BeginEditingSelectedBlueprint());

  model.active_blueprint()->name = "Changed";

  EXPECT_EQ(model.blueprints().begin()->second.name, "Player");
}

TEST(BlueprintPanelModelTest, CreateFinishesAsExistingBlueprint) {
  BlueprintPanelModel model;
  model.BeginNewBlueprint();

  ASSERT_TRUE(model.is_new_blueprint());
  EXPECT_EQ(model.active_blueprint()->name, "New Blueprint");
  ASSERT_OK(model.BuildSaveRequest());

  ASSERT_OK(model.FinishCreate("generated"));
  EXPECT_FALSE(model.is_new_blueprint());
  EXPECT_EQ(model.active_blueprint()->id, "generated");
  EXPECT_EQ(model.selected_blueprint_id(), "generated");
  EXPECT_EQ(model.FinishCreate("duplicate").code(), absl::StatusCode::kFailedPrecondition);
}

TEST(BlueprintPanelModelTest, StateOperationsValidateIndices) {
  BlueprintPanelModel model;
  EXPECT_EQ(model.AddState().code(), absl::StatusCode::kFailedPrecondition);

  model.BeginNewBlueprint();
  ASSERT_OK(model.AddState());
  ASSERT_OK(model.AddState());
  ASSERT_EQ(model.active_blueprint()->states.size(), 2);
  EXPECT_EQ(model.active_blueprint()->states[0].name, "new state");
  EXPECT_EQ(model.active_blueprint()->states[0].placement_mode, BlueprintPlacementMode::kGrounded);
  EXPECT_OK(model.ValidateStateIndex(1));
  EXPECT_EQ(model.ValidateStateIndex(2).code(), absl::StatusCode::kOutOfRange);

  ASSERT_OK(model.DeleteState(0));
  EXPECT_EQ(model.active_blueprint()->states.size(), 1);
  EXPECT_EQ(model.DeleteState(-1).code(), absl::StatusCode::kOutOfRange);
}

TEST(BlueprintPanelModelTest, FailedLookupDoesNotCreateEditingState) {
  BlueprintPanelModel model;
  model.SetBlueprints({{.id = "player", .name = "Player"}});

  EXPECT_EQ(model.SelectBlueprint("missing").code(), absl::StatusCode::kNotFound);
  EXPECT_EQ(model.BeginEditingSelectedBlueprint().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_FALSE(model.has_active_blueprint());
}

TEST(BlueprintPanelModelTest, DeleteClearsSelectionAndEditingState) {
  BlueprintPanelModel model;
  model.SetBlueprints({{.id = "player", .name = "Player"}});
  ASSERT_OK(model.SelectBlueprint("player"));
  ASSERT_OK(model.BeginEditingSelectedBlueprint());

  model.FinishDelete();

  EXPECT_FALSE(model.has_active_blueprint());
  EXPECT_FALSE(model.has_blueprint_selection());
  EXPECT_EQ(model.BuildSaveRequest().status().code(), absl::StatusCode::kFailedPrecondition);
}

// Back used to discard edits with no prompt. Knowing whether there is anything
// to lose is what lets it only ask when asking is warranted.
TEST(BlueprintPanelModelTest, AFreshlyOpenedBlueprintHasNothingToLose) {
  BlueprintPanelModel model;
  EXPECT_FALSE(model.has_unsaved_changes()) << "nothing is being edited";

  model.SetBlueprints({Blueprint{.id = "a", .name = "Samus"}});
  ASSERT_OK(model.SelectBlueprint("a"));
  ASSERT_OK(model.BeginEditingSelectedBlueprint());

  EXPECT_FALSE(model.has_unsaved_changes());
}

TEST(BlueprintPanelModelTest, EditsToAnyPartOfABlueprintCount) {
  BlueprintPanelModel model;
  model.SetBlueprints({Blueprint{.id = "a", .name = "Samus"}});
  ASSERT_OK(model.SelectBlueprint("a"));

  ASSERT_OK(model.BeginEditingSelectedBlueprint());
  model.active_blueprint()->name = "Renamed";
  EXPECT_TRUE(model.has_unsaved_changes());

  ASSERT_OK(model.BeginEditingSelectedBlueprint());
  ASSERT_OK(model.AddState());
  EXPECT_TRUE(model.has_unsaved_changes());
}

TEST(BlueprintPanelModelTest, UndoingAnEditByHandIsCleanAgain) {
  BlueprintPanelModel model;
  model.SetBlueprints({Blueprint{.id = "a", .name = "Samus"}});
  ASSERT_OK(model.SelectBlueprint("a"));
  ASSERT_OK(model.BeginEditingSelectedBlueprint());

  model.active_blueprint()->name = "Renamed";
  ASSERT_TRUE(model.has_unsaved_changes());

  model.active_blueprint()->name = "Samus";
  EXPECT_FALSE(model.has_unsaved_changes());
}

TEST(BlueprintPanelModelTest, SavingMakesTheCurrentStateTheCleanOne) {
  BlueprintPanelModel model;
  model.BeginNewBlueprint();
  model.active_blueprint()->name = "Metroid";
  ASSERT_TRUE(model.has_unsaved_changes());

  ASSERT_OK(model.FinishCreate("metroid-id"));
  EXPECT_FALSE(model.has_unsaved_changes());

  ASSERT_OK(model.AddState());
  ASSERT_TRUE(model.has_unsaved_changes());

  model.MarkSaved();
  EXPECT_FALSE(model.has_unsaved_changes());
}

}  // namespace
}  // namespace zebes
