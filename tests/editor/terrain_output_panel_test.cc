#include "editor/terrain_editor/terrain_output_panel.h"

#include <memory>
#include <string>
#include <vector>

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

class TerrainOutputPanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<std::unique_ptr<TerrainOutputPanel>> panel = TerrainOutputPanel::Create(&gui_);
    ASSERT_TRUE(panel.ok()) << panel.status();
    panel_ = *std::move(panel);

    ON_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillByDefault(Invoke([this](const char* label, const char* preview, ImGuiComboFlags) {
          return ScopedCombo(&gui_, label, preview);
        }));
    ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(false));
  }

  absl::StatusOr<TerrainOutputPanel::Action> Render() { return panel_->Render(model_, textures_); }

  NiceMock<MockGui> gui_;
  std::unique_ptr<TerrainOutputPanel> panel_;
  TerrainEditorModel model_;
  std::vector<Texture> textures_;
};

TEST(TerrainOutputPanelCreateTest, RequiresAGui) {
  EXPECT_FALSE(TerrainOutputPanel::Create(nullptr).ok());
}

TEST_F(TerrainOutputPanelTest, ReportsNothingWhenIdle) {
  absl::StatusOr<TerrainOutputPanel::Action> action = Render();
  ASSERT_TRUE(action.ok()) << action.status();
  EXPECT_EQ(*action, TerrainOutputPanel::Action::kNone);
}

TEST_F(TerrainOutputPanelTest, ReportsCreateWhenPressed) {
  ON_CALL(gui_, Button(StrEq("Create##TerrainOut"), _)).WillByDefault(Return(true));

  absl::StatusOr<TerrainOutputPanel::Action> action = Render();
  ASSERT_TRUE(action.ok()) << action.status();
  EXPECT_EQ(*action, TerrainOutputPanel::Action::kCreate);
}

TEST_F(TerrainOutputPanelTest, GeneratingIsReadyOutOfTheBox) {
  EXPECT_CALL(gui_, BeginDisabled(false)).Times(1);
  ASSERT_TRUE(Render().ok());
}

TEST_F(TerrainOutputPanelTest, DisablesCreateWithoutAName) {
  model_.name().clear();
  EXPECT_CALL(gui_, BeginDisabled(true)).Times(1);
  ASSERT_TRUE(Render().ok());
}

// Importing needs both halves: a manifest describing the layout and the artwork
// it describes. Disabling says so before the user has spent effort.
TEST_F(TerrainOutputPanelTest, DisablesCreateWhileAnImportIsIncomplete) {
  model_.SetSource(TerrainEditorModel::Source::kImportManifest);
  EXPECT_CALL(gui_, BeginDisabled(true)).Times(1);
  ASSERT_TRUE(Render().ok());
}

TEST_F(TerrainOutputPanelTest, EnablesCreateOnceAnImportIsComplete) {
  model_.SetSource(TerrainEditorModel::Source::kImportManifest);
  model_.manifest_path() = "/tmp/grass.json";
  model_.texture_id() = "tex-1";

  EXPECT_CALL(gui_, BeginDisabled(false)).Times(1);
  ASSERT_TRUE(Render().ok());
}

// The texture picker only makes sense for artwork that already exists;
// generating writes its own.
TEST_F(TerrainOutputPanelTest, OffersTheTexturePickerOnlyWhenImporting) {
  textures_.push_back(Texture{.id = "tex-1", .name = "grass_blob47"});

  // The source selector is a combo too, so it needs somewhere to land before
  // the texture picker can be asserted on specifically.
  EXPECT_CALL(gui_, CreateScopedCombo(_, _, _)).Times(::testing::AnyNumber());
  EXPECT_CALL(gui_, CreateScopedCombo(StrEq("Texture##TerrainOut"), _, _)).Times(0);
  ASSERT_TRUE(Render().ok());
  ::testing::Mock::VerifyAndClearExpectations(&gui_);

  ON_CALL(gui_, CreateScopedCombo(_, _, _))
      .WillByDefault(Invoke([this](const char* label, const char* preview, ImGuiComboFlags) {
        return ScopedCombo(&gui_, label, preview);
      }));
  model_.SetSource(TerrainEditorModel::Source::kImportManifest);
  EXPECT_CALL(gui_, CreateScopedCombo(_, _, _)).Times(::testing::AnyNumber());
  EXPECT_CALL(gui_, CreateScopedCombo(StrEq("Texture##TerrainOut"), _, _)).Times(1);
  ASSERT_TRUE(Render().ok());
}

// Quality only affects the written artwork, which is why it sits here rather
// than with the controls that change the picture.
TEST_F(TerrainOutputPanelTest, OffersQualityOnlyWhenGenerating) {
  EXPECT_CALL(gui_, SliderInt(StrEq("Quality##TerrainOut"), _, _, _, _, _)).Times(1);
  ASSERT_TRUE(Render().ok());

  model_.SetSource(TerrainEditorModel::Source::kImportManifest);
  EXPECT_CALL(gui_, SliderInt(StrEq("Quality##TerrainOut"), _, _, _, _, _)).Times(0);
  ASSERT_TRUE(Render().ok());
}

TEST_F(TerrainOutputPanelTest, LoadedRecipeRegeneratesInsteadOfCreatingNewIds) {
  model_.LoadRecipe(TerrainRecipe{.id = "recipe-1",
                                  .name = "Meadow",
                                  .tileset_id = "tileset-1",
                                  .texture_id = "texture-1",
                                  .terrain_id = 1});
  ON_CALL(gui_, Button(StrEq("Regenerate##TerrainOut"), _)).WillByDefault(Return(true));

  const absl::StatusOr<TerrainOutputPanel::Action> action = Render();
  ASSERT_TRUE(action.ok()) << action.status();
  EXPECT_EQ(*action, TerrainOutputPanel::Action::kRegenerate);
}

TEST_F(TerrainOutputPanelTest, SaveAsTurnsALoadedRecipeIntoACopyAction) {
  model_.LoadRecipe(TerrainRecipe{.id = "recipe-1",
                                  .name = "Meadow",
                                  .tileset_id = "tileset-1",
                                  .texture_id = "texture-1",
                                  .terrain_id = 1});
  ON_CALL(gui_, Button(StrEq("Save As##TerrainOut"), _)).WillByDefault(Return(true));

  const absl::StatusOr<TerrainOutputPanel::Action> action = Render();
  ASSERT_TRUE(action.ok()) << action.status();
  EXPECT_EQ(*action, TerrainOutputPanel::Action::kCopyRecipe);
}

}  // namespace
}  // namespace zebes
