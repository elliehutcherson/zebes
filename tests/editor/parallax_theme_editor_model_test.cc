#include "editor/parallax_theme_editor/parallax_theme_editor_model.h"

#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

TEST(ParallaxThemeEditorModelTest, ReorderMovesSelectionWithTheLayer) {
  ParallaxThemeEditorModel model;
  model.Open({
      .id = "theme",
      .name = "Cave",
      .layers = {{.name = "Far", .texture_id = "far"}, {.name = "Near", .texture_id = "near"}},
  });
  model.SelectLayer(0);
  ASSERT_OK(model.MoveSelectedLayer(1));
  ASSERT_EQ(model.selected_layer(), 1);
  EXPECT_EQ(model.draft()->layers[1].name, "Far");
}

TEST(ParallaxThemeEditorModelTest, DeletionReconcilesSelection) {
  ParallaxThemeEditorModel model;
  model.Open({
      .id = "theme",
      .name = "Cave",
      .layers = {{.name = "Far", .texture_id = "far"}, {.name = "Near", .texture_id = "near"}},
  });
  model.SelectLayer(1);
  ASSERT_OK(model.DeleteSelectedLayer());
  EXPECT_EQ(model.selected_layer(), 0);
  EXPECT_EQ(model.draft()->layers[0].name, "Far");
}

TEST(ParallaxThemeEditorModelTest, IncompleteDraftCannotBecomeASaveRequest) {
  ParallaxThemeEditorModel model;
  model.BeginNew();
  EXPECT_EQ(model.BuildSaveRequest().status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
