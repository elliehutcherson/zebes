#include "editor/parallax_theme_editor/parallax_theme_editor_model.h"

#include <limits>

#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

ParallaxLayer Layer(std::string name, std::string texture_id) {
  return {
      .name = std::move(name),
      .elements = {{.id = 0, .name = "Element", .texture_id = std::move(texture_id)}},
  };
}

TEST(ParallaxThemeEditorModelTest, ReorderMovesSelectionWithTheLayer) {
  ParallaxThemeEditorModel model;
  model.Open({
      .id = "theme",
      .name = "Cave",
      .layers = {Layer("Far", "far"), Layer("Near", "near")},
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
      .layers = {Layer("Far", "far"), Layer("Near", "near")},
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

TEST(ParallaxThemeEditorModelTest, DiscardChangesRestoresTheSavedSnapshotInPlace) {
  ParallaxThemeEditorModel model;
  model.Open({
      .id = "theme",
      .name = "Cave",
      .layers = {Layer("Far", "saved-texture"), Layer("Near", "near")},
  });
  model.SelectLayer(1);
  model.draft()->name = "Accidental rename";
  model.draft()->layers[1].elements[0].texture_id = "accidental-texture";
  ASSERT_TRUE(model.dirty());

  ASSERT_OK(model.DiscardChanges());

  EXPECT_FALSE(model.dirty());
  EXPECT_EQ(model.draft()->name, "Cave");
  EXPECT_EQ(model.draft()->layers[1].elements[0].texture_id, "near");
  EXPECT_EQ(model.selected_layer(), 1);
}

TEST(ParallaxThemeEditorModelTest, DiscardChangesResetsANewDraft) {
  ParallaxThemeEditorModel model;
  model.BeginNew();
  model.draft()->name = "Accidental rename";

  ASSERT_OK(model.DiscardChanges());

  ASSERT_TRUE(model.is_new());
  EXPECT_EQ(model.draft()->name, "New Theme");
  ASSERT_EQ(model.draft()->layers.size(), 1);
  EXPECT_EQ(model.selected_layer(), 0);
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
                  .scroll_factor = {0.9, 0.8},
                  .offset = {12, 34},
                  .elements = {{.id = 0, .name = "Element", .texture_id = "far", .scale = 2.0f}}}},
  });

  ASSERT_OK(model.ApplyDepthPreset(ParallaxDepthPreset::kFar));
  EXPECT_EQ(model.draft()->layers[0].scroll_factor, Vec(0.05, 0.05));
  EXPECT_EQ(model.draft()->layers[0].offset, Vec(12, 34));
  EXPECT_FLOAT_EQ(model.draft()->layers[0].elements[0].scale, 2.0f);

  ASSERT_OK(model.ApplyDepthPreset(ParallaxDepthPreset::kMiddle));
  EXPECT_EQ(model.draft()->layers[0].scroll_factor, Vec(0.20, 0.10));

  ASSERT_OK(model.ApplyDepthPreset(ParallaxDepthPreset::kNearBackground));
  EXPECT_EQ(model.draft()->layers[0].scroll_factor, Vec(0.50, 0.25));
}

TEST(ParallaxThemeEditorModelTest, ElementSelectionUsesStableIdsAcrossReorderAndDelete) {
  ParallaxThemeEditorModel model;
  ParallaxLayer layer = Layer("Near", "left");
  layer.elements.push_back({.id = 5, .name = "Right", .texture_id = "right"});
  model.Open({.id = "theme", .name = "Cave", .layers = {layer}});
  model.SelectElement(5);

  ASSERT_OK(model.MoveSelectedElement(-1));
  EXPECT_EQ(model.selected_element_id(), 5);
  EXPECT_EQ(model.draft()->layers[0].elements[0].id, 5);

  ASSERT_OK(model.DeleteSelectedElement());
  EXPECT_EQ(model.selected_element_id(), 0);
}

TEST(ParallaxThemeEditorModelTest, DuplicateElementGetsIndependentStableIdentity) {
  ParallaxThemeEditorModel model;
  model.Open({.id = "theme", .name = "Cave", .layers = {Layer("Near", "near")}});

  ASSERT_OK(model.DuplicateSelectedElement());

  ASSERT_EQ(model.draft()->layers[0].elements.size(), 2);
  EXPECT_EQ(model.draft()->layers[0].elements[0].id, 0);
  EXPECT_EQ(model.draft()->layers[0].elements[1].id, 1);
  EXPECT_EQ(model.selected_element_id(), 1);
  EXPECT_EQ(model.draft()->layers[0].elements[1].texture_id, "near");
}

TEST(ParallaxThemeEditorModelTest, AddingElementUsesAnAvailableIdWithoutOverflow) {
  ParallaxThemeEditorModel model;
  ParallaxLayer layer = Layer("Near", "left");
  layer.elements[0].id = std::numeric_limits<int>::max();
  model.Open({.id = "theme", .name = "Cave", .layers = {layer}});

  ASSERT_OK(model.AddElement());

  ASSERT_EQ(model.draft()->layers[0].elements.size(), 2);
  EXPECT_EQ(model.draft()->layers[0].elements[1].id, 0);
}

TEST(ParallaxThemeEditorModelTest, DepthPresetRequiresASelectedLayer) {
  ParallaxThemeEditorModel model;
  EXPECT_EQ(model.ApplyDepthPreset(ParallaxDepthPreset::kFar).code(),
            absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
