#include "editor/level_editor/level_panel_model.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace zebes {
namespace {

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

}  // namespace
}  // namespace zebes
