#include "editor/blueprint_editor/blueprint_panel_model.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"

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
  model.SetBlueprints(
      {{.id = "player", .name = "Z Player"}, {.id = "enemy", .name = "A Enemy"}});
  ASSERT_TRUE(model.SelectBlueprint("player").ok());

  model.SetBlueprints(
      {{.id = "player", .name = "A Player"}, {.id = "enemy", .name = "Z Enemy"}});

  EXPECT_EQ(model.selected_blueprint_id(), "player");
  ASSERT_TRUE(model.BeginEditingSelectedBlueprint().ok());
  ASSERT_NE(model.active_blueprint(), nullptr);
  EXPECT_EQ(model.active_blueprint()->id, "player");
}

TEST(BlueprintPanelModelTest, EditingUsesCopyInsteadOfCatalogStorage) {
  BlueprintPanelModel model;
  model.SetBlueprints({{.id = "player", .name = "Player"}});
  ASSERT_TRUE(model.SelectBlueprint("player").ok());
  ASSERT_TRUE(model.BeginEditingSelectedBlueprint().ok());

  model.active_blueprint()->name = "Changed";

  EXPECT_EQ(model.blueprints().begin()->second.name, "Player");
}

TEST(BlueprintPanelModelTest, CreateFinishesAsExistingBlueprint) {
  BlueprintPanelModel model;
  model.BeginNewBlueprint();

  ASSERT_TRUE(model.is_new_blueprint());
  EXPECT_EQ(model.active_blueprint()->name, "New Blueprint");
  ASSERT_TRUE(model.BuildSaveRequest().ok());

  ASSERT_TRUE(model.FinishCreate("generated").ok());
  EXPECT_FALSE(model.is_new_blueprint());
  EXPECT_EQ(model.active_blueprint()->id, "generated");
  EXPECT_EQ(model.selected_blueprint_id(), "generated");
  EXPECT_EQ(model.FinishCreate("duplicate").code(), absl::StatusCode::kFailedPrecondition);
}

TEST(BlueprintPanelModelTest, StateOperationsValidateIndices) {
  BlueprintPanelModel model;
  EXPECT_EQ(model.AddState().code(), absl::StatusCode::kFailedPrecondition);

  model.BeginNewBlueprint();
  ASSERT_TRUE(model.AddState().ok());
  ASSERT_TRUE(model.AddState().ok());
  ASSERT_EQ(model.active_blueprint()->states.size(), 2);
  EXPECT_EQ(model.active_blueprint()->states[0].name, "new state");
  EXPECT_TRUE(model.ValidateStateIndex(1).ok());
  EXPECT_EQ(model.ValidateStateIndex(2).code(), absl::StatusCode::kOutOfRange);

  ASSERT_TRUE(model.DeleteState(0).ok());
  EXPECT_EQ(model.active_blueprint()->states.size(), 1);
  EXPECT_EQ(model.DeleteState(-1).code(), absl::StatusCode::kOutOfRange);
}

TEST(BlueprintPanelModelTest, FailedLookupDoesNotCreateEditingState) {
  BlueprintPanelModel model;
  model.SetBlueprints({{.id = "player", .name = "Player"}});

  EXPECT_EQ(model.SelectBlueprint("missing").code(), absl::StatusCode::kNotFound);
  EXPECT_EQ(model.BeginEditingSelectedBlueprint().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_FALSE(model.has_active_blueprint());
}

TEST(BlueprintPanelModelTest, DeleteClearsSelectionAndEditingState) {
  BlueprintPanelModel model;
  model.SetBlueprints({{.id = "player", .name = "Player"}});
  ASSERT_TRUE(model.SelectBlueprint("player").ok());
  ASSERT_TRUE(model.BeginEditingSelectedBlueprint().ok());

  model.FinishDelete();

  EXPECT_FALSE(model.has_active_blueprint());
  EXPECT_FALSE(model.has_blueprint_selection());
  EXPECT_EQ(model.BuildSaveRequest().status().code(),
            absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
