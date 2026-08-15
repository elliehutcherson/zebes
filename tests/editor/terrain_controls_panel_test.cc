#include "editor/terrain_editor/terrain_controls_panel.h"

#include <memory>
#include <optional>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

class TerrainControlsPanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<std::unique_ptr<TerrainControlsPanel>> panel =
        TerrainControlsPanel::Create(&gui_);
    ASSERT_TRUE(panel.ok()) << panel.status();
    panel_ = *std::move(panel);

    // Sections open by default, so the controls inside them actually run.
    ON_CALL(gui_, CollapsingHeader(_, _)).WillByDefault(Return(true));
    ON_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillByDefault(Invoke([this](const char* label, const char* preview, ImGuiComboFlags) {
          return ScopedCombo(&gui_, label, preview);
        }));
    ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(false));
  }

  NiceMock<MockGui> gui_;
  std::unique_ptr<TerrainControlsPanel> panel_;
  TerrainEditorModel model_;
};

TEST(TerrainControlsPanelCreateTest, RequiresAGui) {
  EXPECT_FALSE(TerrainControlsPanel::Create(nullptr).ok());
}

TEST_F(TerrainControlsPanelTest, ReportsNoChangeWhenNothingMoves) {
  EXPECT_FALSE(panel_->Render(model_));
}

TEST_F(TerrainControlsPanelTest, ReportsAChangeWhenASliderMoves) {
  ON_CALL(gui_, SliderFloat(StrEq("Top depth##TerrainGen"), _, _, _, _, _))
      .WillByDefault(
          Invoke([](const char*, float* value, float, float, const char*, ImGuiSliderFlags) {
            *value = 12.0f;
            return true;
          }));

  EXPECT_TRUE(panel_->Render(model_));
  EXPECT_EQ(model_.config().surface.top_depth, 12.0f);
}

TEST_F(TerrainControlsPanelTest, EdgeDetailFamilyAndShapeAreEditable) {
  ON_CALL(gui_, BeginCombo(StrEq("Edge details##TerrainSurface"), _, _))
      .WillByDefault(Return(true));
  ON_CALL(gui_, Selectable(StrEq("Dry grass"), false, _, _)).WillByDefault(Return(true));
  ON_CALL(gui_, SliderInt(StrEq("Edge length##TerrainGen"), _, _, _, _, _))
      .WillByDefault(Invoke([](const char*, int* value, int, int, const char*, ImGuiSliderFlags) {
        *value = 7;
        return true;
      }));

  EXPECT_TRUE(panel_->Render(model_));
  EXPECT_EQ(model_.config().surface.edge_detail.family, TerrainEdgeDetailSet::kDryGrass);
  EXPECT_EQ(model_.config().surface.edge_detail.length, 7);
  EXPECT_FALSE(model_.selected_preset().has_value());
}

TEST_F(TerrainControlsPanelTest, AVisualPresetReplacesTheWholeArtConfiguration) {
  model_.config().supersample = 2;
  model_.config().seed = 999;
  ON_CALL(gui_, BeginCombo(StrEq("Preset##TerrainGen"), _, _)).WillByDefault(Return(true));
  ON_CALL(gui_, Selectable(StrEq("Cozy Meadow"), false, _, _)).WillByDefault(Return(true));

  EXPECT_TRUE(panel_->Render(model_));
  EXPECT_EQ(model_.config().material.name, "Cozy Meadow");
  EXPECT_EQ(model_.config().interior.base.style, TerrainInteriorStyle::kSoilClods);
  EXPECT_EQ(model_.config().interior.pattern.family, TerrainSubstratePattern::kMixedEarth);
  EXPECT_EQ(model_.config().interior.details.family, TerrainDetailSet::kMeadow);
  EXPECT_EQ(model_.config().variant_period, 3);
  // Output quality and pattern seed are intentional author choices, not part
  // of a material preset.
  EXPECT_EQ(model_.config().supersample, 2);
  EXPECT_EQ(model_.config().seed, 999u);
  ASSERT_TRUE(model_.selected_preset().has_value());
  EXPECT_EQ(*model_.selected_preset(), "Cozy Meadow");
}

TEST_F(TerrainControlsPanelTest, AManualArtEditMarksThePresetAsCustom) {
  ASSERT_TRUE(model_.selected_preset().has_value());
  ON_CALL(gui_, SliderFloat(StrEq("Contrast##TerrainGen"), _, _, _, _, _))
      .WillByDefault(
          Invoke([](const char*, float* value, float, float, const char*, ImGuiSliderFlags) {
            *value = 1.2f;
            return true;
          }));

  EXPECT_TRUE(panel_->Render(model_));
  EXPECT_FALSE(model_.selected_preset().has_value());
}

TEST_F(TerrainControlsPanelTest, ChangingOnlyTheSeedKeepsTheSelectedPreset) {
  ON_CALL(gui_, InputInt(StrEq("Seed##TerrainGen"), _, _, _, _))
      .WillByDefault(Invoke([](const char*, int* value, int, int, ImGuiInputTextFlags) {
        *value = 77;
        return true;
      }));

  EXPECT_TRUE(panel_->Render(model_));
  ASSERT_TRUE(model_.selected_preset().has_value());
  EXPECT_EQ(*model_.selected_preset(), "Classic Grass");
}

TEST_F(TerrainControlsPanelTest, HidesControlsForDisabledInteriorLayers) {
  model_.config().interior.base.style = TerrainInteriorStyle::kFlat;
  model_.config().interior.pattern.family = TerrainSubstratePattern::kNone;
  model_.config().interior.details.family = TerrainDetailSet::kNone;
  ON_CALL(gui_, CollapsingHeader(_, _))
      .WillByDefault(Invoke([](const char* label, ImGuiTreeNodeFlags) {
        return std::string(label) == "Interior##TerrainGen";
      }));

  // The family combos themselves still render; it is the per-layer controls
  // below them that must not.
  EXPECT_CALL(gui_, CreateScopedCombo(_, _, _)).Times(AnyNumber());

  EXPECT_CALL(gui_, SliderFloat(StrEq("Mottle size##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderFloat(StrEq("Mottle amount##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderFloat(StrEq("Feature size##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderFloat(StrEq("Relief##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderInt(StrEq("Pattern amount##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderInt(StrEq("Pattern spacing##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderFloat(StrEq("Pattern contrast##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderInt(StrEq("Detail amount##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderInt(StrEq("Detail spacing##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderInt(StrEq("Pattern size##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderInt(StrEq("Pattern margin##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderInt(StrEq("Detail size##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderInt(StrEq("Detail margin##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, CreateScopedCombo(StrEq("Pattern accent##TerrainGen"), _, _)).Times(0);
  EXPECT_CALL(gui_, CreateScopedCombo(StrEq("Detail accent##TerrainGen"), _, _)).Times(0);

  EXPECT_FALSE(panel_->Render(model_));
}

TEST_F(TerrainControlsPanelTest, ChangingMotifSizeMarksThePresetAsCustom) {
  model_.config().interior.pattern.family = TerrainSubstratePattern::kDiamonds;
  ASSERT_TRUE(model_.selected_preset().has_value());
  ON_CALL(gui_, SliderInt(StrEq("Pattern size##TerrainGen"), _, _, _, _, _))
      .WillByDefault(Invoke([](const char*, int* value, int, int, const char*, ImGuiSliderFlags) {
        *value = 3;
        return true;
      }));

  EXPECT_TRUE(panel_->Render(model_));
  EXPECT_EQ(model_.config().interior.pattern.scale, 3);
  EXPECT_FALSE(model_.selected_preset().has_value());
}

// The margin fields have always been honoured by the renderer but had no
// widget, so a terrain could not be tuned away from the default clearance.
TEST_F(TerrainControlsPanelTest, MotifMarginIsAdjustable) {
  model_.config().interior.details.family = TerrainDetailSet::kCrystals;
  ON_CALL(gui_, SliderInt(StrEq("Detail margin##TerrainGen"), _, _, _, _, _))
      .WillByDefault(Invoke([](const char*, int* value, int, int, const char*, ImGuiSliderFlags) {
        *value = 4;
        return true;
      }));

  EXPECT_TRUE(panel_->Render(model_));
  EXPECT_EQ(model_.config().interior.details.margin, 4);
}

TEST_F(TerrainControlsPanelTest, SelectsAnAccentModeForEachMotifLayer) {
  model_.config().interior.pattern.family = TerrainSubstratePattern::kDiamonds;
  ON_CALL(gui_, BeginCombo(StrEq("Pattern accent##TerrainGen"), _, _)).WillByDefault(Return(true));
  ON_CALL(gui_, Selectable(StrEq("Gradient"), false, _, _)).WillByDefault(Return(true));

  EXPECT_TRUE(panel_->Render(model_));
  EXPECT_EQ(model_.config().interior.pattern.accent_mode, TerrainAccentMode::kGradient);
  EXPECT_FALSE(model_.selected_preset().has_value());
}

// Contrast only shapes the substrate ramp, which the accent modes bypass. A
// live slider that silently does nothing is worse than a disabled one.
TEST_F(TerrainControlsPanelTest, PatternContrastIsDisabledInAccentModes) {
  model_.config().interior.pattern.family = TerrainSubstratePattern::kDiamonds;
  model_.config().interior.pattern.accent_mode = TerrainAccentMode::kGradient;

  EXPECT_CALL(gui_, CreateScopedDisabled(true)).Times(1);

  EXPECT_FALSE(panel_->Render(model_));
}

TEST_F(TerrainControlsPanelTest, PatternContrastStaysLiveInMaterialMode) {
  model_.config().interior.pattern.family = TerrainSubstratePattern::kDiamonds;
  model_.config().interior.pattern.accent_mode = TerrainAccentMode::kMaterial;

  EXPECT_CALL(gui_, CreateScopedDisabled(true)).Times(0);
  EXPECT_CALL(gui_, CreateScopedDisabled(false)).Times(1);

  EXPECT_FALSE(panel_->Render(model_));
}

// Picking Crystals and getting substrate-tinted gems would be a surprise, so
// the family carries its usual accent mode with it.
TEST_F(TerrainControlsPanelTest, ChoosingADetailFamilyAdoptsItsUsualAccentMode) {
  ASSERT_EQ(model_.config().interior.details.accent_mode, TerrainAccentMode::kMaterial);
  ON_CALL(gui_, BeginCombo(StrEq("Details##TerrainGen"), _, _)).WillByDefault(Return(true));
  ON_CALL(gui_, Selectable(StrEq("Crystals"), false, _, _)).WillByDefault(Return(true));

  EXPECT_TRUE(panel_->Render(model_));
  EXPECT_EQ(model_.config().interior.details.family, TerrainDetailSet::kCrystals);
  EXPECT_EQ(model_.config().interior.details.accent_mode, TerrainAccentMode::kAccent);
}

TEST_F(TerrainControlsPanelTest, SelectsARicherSubstrateFamily) {
  ON_CALL(gui_, BeginCombo(StrEq("Pattern##TerrainInterior"), _, _)).WillByDefault(Return(true));
  ON_CALL(gui_, Selectable(StrEq("Crosses"), false, _, _)).WillByDefault(Return(true));

  EXPECT_TRUE(panel_->Render(model_));
  EXPECT_EQ(model_.config().interior.pattern.family, TerrainSubstratePattern::kCrosses);
  EXPECT_FALSE(model_.selected_preset().has_value());
}

// A collapsed section must not swallow the fact that another one moved.
TEST_F(TerrainControlsPanelTest, ReportsAChangeFromASectionBelowACollapsedOne) {
  ON_CALL(gui_, CollapsingHeader(StrEq("Material##TerrainGen"), _)).WillByDefault(Return(false));
  ON_CALL(gui_, SliderInt(StrEq("Tile size##TerrainGen"), _, _, _, _, _))
      .WillByDefault(Invoke([](const char*, int* value, int, int, const char*, ImGuiSliderFlags) {
        *value = 16;
        return true;
      }));

  EXPECT_TRUE(panel_->Render(model_));
  EXPECT_EQ(model_.config().tile_size, 16);
}

// Importing describes artwork that already exists, so the tuning controls are
// meaningless and the panel shows the manifest instead.
TEST_F(TerrainControlsPanelTest, ShowsManifestControlsWhenImporting) {
  model_.SetSource(TerrainEditorModel::Source::kImportManifest);

  EXPECT_CALL(gui_, Button(StrEq("Browse##TerrainManifest"), _)).Times(1);
  EXPECT_CALL(gui_, SliderFloat(StrEq("Top depth##TerrainGen"), _, _, _, _, _)).Times(0);

  panel_->Render(model_);
}

TEST_F(TerrainControlsPanelTest, PicksUpAManifestChosenInTheFileDialog) {
  model_.SetSource(TerrainEditorModel::Source::kImportManifest);
  ON_CALL(gui_, DisplayFileDialog(_))
      .WillByDefault(Return(std::optional<std::string>("/tmp/grass.json")));

  EXPECT_TRUE(panel_->Render(model_));
  EXPECT_EQ(model_.manifest_path(), "/tmp/grass.json");
}

}  // namespace
}  // namespace zebes
