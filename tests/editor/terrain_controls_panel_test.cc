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
  ON_CALL(gui_, SliderFloat(StrEq("Depth##TerrainGen"), _, _, _, _, _))
      .WillByDefault(
          Invoke([](const char*, float* value, float, float, const char*, ImGuiSliderFlags) {
            *value = 12.0f;
            return true;
          }));

  EXPECT_TRUE(panel_->Render(model_));
  EXPECT_EQ(model_.config().grass_band, 12.0f);
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

  EXPECT_CALL(gui_, SliderFloat(StrEq("Mottle size##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderFloat(StrEq("Mottle amount##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderFloat(StrEq("Feature size##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderFloat(StrEq("Relief##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderInt(StrEq("Pattern amount##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderInt(StrEq("Pattern spacing##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderFloat(StrEq("Pattern contrast##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderInt(StrEq("Detail amount##TerrainGen"), _, _, _, _, _)).Times(0);
  EXPECT_CALL(gui_, SliderInt(StrEq("Detail spacing##TerrainGen"), _, _, _, _, _)).Times(0);

  EXPECT_FALSE(panel_->Render(model_));
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
  EXPECT_CALL(gui_, SliderFloat(StrEq("Depth##TerrainGen"), _, _, _, _, _)).Times(0);

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
