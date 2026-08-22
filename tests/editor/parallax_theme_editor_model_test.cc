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

TEST(ParallaxThemeEditorModelTest, NewLayersStartAtTheMiddleBackgroundPreset) {
  ParallaxThemeEditorModel model;
  model.BeginNew();

  ASSERT_EQ(model.draft()->layers.size(), 1);
  EXPECT_EQ(model.draft()->layers[0].scroll_factor, Vec(0.20, 0.10));
}

TEST(ParallaxThemeEditorModelTest, DepthPresetsChangeOnlySelectedLayerScrollFactors) {
  ParallaxThemeEditorModel model;
  model.Open({
      .id = "theme",
      .name = "Cave",
      .layers = {{.name = "Far",
                  .texture_id = "far",
                  .scroll_factor = {0.9, 0.8},
                  .offset = {12, 34},
                  .base_scale = 2.0f}},
  });

  ASSERT_OK(model.ApplyDepthPreset(ParallaxDepthPreset::kFar));
  EXPECT_EQ(model.draft()->layers[0].scroll_factor, Vec(0.05, 0.05));
  EXPECT_EQ(model.draft()->layers[0].offset, Vec(12, 34));
  EXPECT_FLOAT_EQ(model.draft()->layers[0].base_scale, 2.0f);

  ASSERT_OK(model.ApplyDepthPreset(ParallaxDepthPreset::kMiddle));
  EXPECT_EQ(model.draft()->layers[0].scroll_factor, Vec(0.20, 0.10));

  ASSERT_OK(model.ApplyDepthPreset(ParallaxDepthPreset::kNearBackground));
  EXPECT_EQ(model.draft()->layers[0].scroll_factor, Vec(0.50, 0.25));
}

TEST(ParallaxThemeEditorModelTest, DepthPresetRequiresASelectedLayer) {
  ParallaxThemeEditorModel model;
  EXPECT_EQ(model.ApplyDepthPreset(ParallaxDepthPreset::kFar).code(),
            absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
