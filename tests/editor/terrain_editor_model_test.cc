#include "editor/terrain_editor/terrain_editor_model.h"

#include <algorithm>

#include "gtest/gtest.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

TerrainRecipe SavedRecipe() {
  TerrainRecipe recipe{.id = "recipe-1",
                       .name = "Saved Meadow",
                       .tileset_id = "tileset-1",
                       .texture_id = "texture-1",
                       .terrain_id = 3};
  recipe.source_preset = "Cozy Meadow";
  recipe.config.seed = 88;
  return recipe;
}

// Small tiles keep every refresh in this test in the milliseconds.
TerrainEditorModel MakeModel() {
  TerrainEditorModel model;
  model.config().tile_size = 8;
  model.config().supersample = 1;
  return model;
}

TEST(TerrainEditorModelTest, StartsOnGenerateWithAName) {
  const TerrainEditorModel model;
  EXPECT_EQ(model.source(), TerrainEditorModel::Source::kGenerate);
  EXPECT_FALSE(model.name().empty()) << "an unnamed default would disable Create on arrival";
  EXPECT_FALSE(model.preview().has_value());
  ASSERT_TRUE(model.selected_preset().has_value());
  EXPECT_EQ(*model.selected_preset(), "Classic Grass");
}

TEST(TerrainEditorModelTest, ApplyingAPresetPreservesSeedAndQuality) {
  TerrainEditorModel model;
  model.config().seed = 88;
  model.config().supersample = 2;
  const absl::Span<const TerrainPreset> presets = BuiltInTerrainPresets();
  const auto cozy = std::find_if(presets.begin(), presets.end(), [](const TerrainPreset& preset) {
    return preset.name == "Cozy Meadow";
  });
  ASSERT_NE(cozy, presets.end());

  model.ApplyPreset(*cozy);

  EXPECT_EQ(model.config().seed, 88u);
  EXPECT_EQ(model.config().supersample, 2);
  EXPECT_EQ(model.config().material.name, "Cozy Meadow");
  ASSERT_TRUE(model.selected_preset().has_value());
  EXPECT_EQ(*model.selected_preset(), "Cozy Meadow");
}

// Generation writes one blob tile per mask per phase. It bakes no slope units,
// so counting them here would promise the user tiles that Create never writes.
TEST(TerrainEditorModelTest, TileCountCountsEveryPhaseAndNoSlopes) {
  TerrainEditorModel model = MakeModel();
  EXPECT_EQ(model.TileCount(), kBlob47TileCount);

  model.config().variant_period = 2;
  EXPECT_EQ(model.TileCount(), kBlob47TileCount * 4);
}

// The picture has to arrive without the user touching anything.
TEST(TerrainEditorModelTest, DrawsAPreviewOnTheFirstRefresh) {
  TerrainEditorModel model = MakeModel();
  ASSERT_TRUE(model.RefreshPreviewIfNeeded(/*interacting=*/false).ok());
  ASSERT_TRUE(model.preview().has_value());
  EXPECT_GT(model.preview()->width, 0);
}

// Redrawing an unchanged scene every frame would spend the whole frame budget
// on a picture nobody asked to change.
TEST(TerrainEditorModelTest, DoesNotRedrawWhenNothingChanged) {
  TerrainEditorModel model = MakeModel();
  ASSERT_TRUE(model.RefreshPreviewIfNeeded(false).ok());
  ASSERT_TRUE(model.RefreshPreviewIfNeeded(false).ok());

  const int width = model.preview()->width;
  model.config().tile_size = 16;  // Changed behind the model's back.
  ASSERT_TRUE(model.RefreshPreviewIfNeeded(false).ok());
  EXPECT_EQ(model.preview()->width, width)
      << "the model redrew without being told the configuration moved";
}

TEST(TerrainEditorModelTest, RedrawsAfterBeingMarkedStale) {
  TerrainEditorModel model = MakeModel();
  ASSERT_TRUE(model.RefreshPreviewIfNeeded(false).ok());
  const int width = model.preview()->width;

  model.config().tile_size = 16;
  model.MarkPreviewStale();
  ASSERT_TRUE(model.RefreshPreviewIfNeeded(false).ok());
  EXPECT_NE(model.preview()->width, width);
}

// While a control is held the preview stays at draft quality; letting go
// settles it. Getting this backwards is what made dragging a slider stall.
TEST(TerrainEditorModelTest, SettlesToFullQualityOnlyAfterInteractionEnds) {
  TerrainEditorModel model = MakeModel();
  model.config().supersample = 4;

  // A change while dragging draws a draft, and holding does not settle it.
  model.MarkPreviewStale();
  ASSERT_TRUE(model.RefreshPreviewIfNeeded(/*interacting=*/true).ok());
  ASSERT_TRUE(model.preview().has_value());
  const std::vector<uint8_t> draft = model.preview()->pixels;

  ASSERT_TRUE(model.RefreshPreviewIfNeeded(/*interacting=*/true).ok());
  EXPECT_EQ(model.preview()->pixels, draft) << "settled while the control was still held";

  // Releasing settles it, and once settled it stays put.
  ASSERT_TRUE(model.RefreshPreviewIfNeeded(/*interacting=*/false).ok());
  const std::vector<uint8_t> settled = model.preview()->pixels;
  EXPECT_NE(settled, draft) << "releasing did not redraw at full quality";

  ASSERT_TRUE(model.RefreshPreviewIfNeeded(false).ok());
  EXPECT_EQ(model.preview()->pixels, settled) << "redrew a preview that was already final";
}

TEST(TerrainEditorModelTest, ReportsAConfigurationItCannotDraw) {
  TerrainEditorModel model = MakeModel();
  model.config().variant_period = 0;
  model.MarkPreviewStale();

  EXPECT_FALSE(model.RefreshPreviewIfNeeded(false).ok());
  EXPECT_FALSE(model.preview().has_value())
      << "a failed configuration should show nothing, not the last one that worked";
}

// An imported terrain's artwork already exists, so there is nothing to draw and
// nothing to spend time drawing.
TEST(TerrainEditorModelTest, ImportingDropsThePreview) {
  TerrainEditorModel model = MakeModel();
  ASSERT_TRUE(model.RefreshPreviewIfNeeded(false).ok());
  ASSERT_TRUE(model.preview().has_value());

  model.SetSource(TerrainEditorModel::Source::kImportManifest);
  EXPECT_FALSE(model.preview().has_value());

  ASSERT_TRUE(model.RefreshPreviewIfNeeded(false).ok());
  EXPECT_FALSE(model.preview().has_value());
}

TEST(TerrainEditorModelTest, ReturningToGenerateRedrawsThePreview) {
  TerrainEditorModel model = MakeModel();
  model.SetSource(TerrainEditorModel::Source::kImportManifest);
  model.SetSource(TerrainEditorModel::Source::kGenerate);

  ASSERT_TRUE(model.RefreshPreviewIfNeeded(false).ok());
  EXPECT_TRUE(model.preview().has_value());
}

TEST(TerrainEditorModelTest, LoadingARecipeRestoresConfigAndAssetBinding) {
  TerrainEditorModel model;
  const TerrainRecipe recipe = SavedRecipe();
  model.LoadRecipe(recipe);

  ASSERT_TRUE(model.active_recipe().has_value());
  EXPECT_EQ(model.active_recipe()->id, recipe.id);
  EXPECT_EQ(model.name(), recipe.name);
  EXPECT_EQ(model.config().seed, 88u);
  EXPECT_EQ(model.selected_preset(), recipe.source_preset);
  EXPECT_EQ(model.source(), TerrainEditorModel::Source::kGenerate);
}

TEST(TerrainEditorModelTest, SaveAsCopyKeepsTheLookButDropsEveryAssetBinding) {
  TerrainEditorModel model;
  model.LoadRecipe(SavedRecipe());
  model.StartRecipeCopy();

  EXPECT_FALSE(model.active_recipe().has_value());
  EXPECT_EQ(model.name(), "Saved Meadow copy");
  EXPECT_EQ(model.config().seed, 88u);
  EXPECT_FALSE(model.result().has_value());
}

TEST(TerrainEditorModelTest, SwitchingARecipeToImportDropsItsGeneratedAssetBinding) {
  TerrainEditorModel model;
  model.LoadRecipe(SavedRecipe());
  model.SetSource(TerrainEditorModel::Source::kImportManifest);

  EXPECT_FALSE(model.active_recipe().has_value());
  EXPECT_FALSE(model.result().has_value());
}

}  // namespace
}  // namespace zebes
