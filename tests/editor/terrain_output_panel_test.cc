#include "editor/terrain_editor/terrain_output_panel.h"

#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Contains;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::Return;
using ::testing::StrEq;

class TerrainOutputPanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<std::unique_ptr<TerrainOutputPanel>> panel = TerrainOutputPanel::Create(&gui_);
    ASSERT_OK(panel);
    panel_ = *std::move(panel);

    ON_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillByDefault(Invoke([this](const char* label, const char* preview, ImGuiComboFlags) {
          return ScopedCombo(&gui_, label, preview);
        }));
    ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(false));

    // The delete prompt tints its buttons, so a render with a recipe open goes
    // through here.
    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(Invoke(
            [this](ImGuiCol idx, const ImVec4& col) { return ScopedStyleColor(&gui_, idx, col); }));
  }

  // Opens a recipe, which is the state the delete control appears in.
  void LoadRecipe() {
    model_.LoadRecipe(TerrainRecipe{.id = "recipe-1",
                                    .name = "Meadow",
                                    .tileset_id = "tileset-1",
                                    .texture_id = "texture-1",
                                    .terrain_id = 1});
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
  ASSERT_OK(action);
  EXPECT_EQ(*action, TerrainOutputPanel::Action::kNone);
}

TEST_F(TerrainOutputPanelTest, ReportsCreateWhenPressed) {
  ON_CALL(gui_, Button(StrEq("Create##TerrainOut"), _)).WillByDefault(Return(true));

  absl::StatusOr<TerrainOutputPanel::Action> action = Render();
  ASSERT_OK(action);
  EXPECT_EQ(*action, TerrainOutputPanel::Action::kCreate);
}

TEST_F(TerrainOutputPanelTest, GeneratingIsReadyOutOfTheBox) {
  EXPECT_CALL(gui_, BeginDisabled(false)).Times(1);
  ASSERT_OK(Render());
}

TEST_F(TerrainOutputPanelTest, DisablesCreateWithoutAName) {
  model_.name().clear();
  EXPECT_CALL(gui_, BeginDisabled(true)).Times(1);
  ASSERT_OK(Render());
}

// Importing needs both halves: a manifest describing the layout and the artwork
// it describes. Disabling says so before the user has spent effort.
TEST_F(TerrainOutputPanelTest, DisablesCreateWhileAnImportIsIncomplete) {
  model_.SetSource(TerrainEditorModel::Source::kImportManifest);
  EXPECT_CALL(gui_, BeginDisabled(true)).Times(1);
  ASSERT_OK(Render());
}

TEST_F(TerrainOutputPanelTest, EnablesCreateOnceAnImportIsComplete) {
  model_.SetSource(TerrainEditorModel::Source::kImportManifest);
  model_.manifest_path() = "/tmp/grass.json";
  model_.texture_id() = "tex-1";

  EXPECT_CALL(gui_, BeginDisabled(false)).Times(1);
  ASSERT_OK(Render());
}

// The texture picker only makes sense for artwork that already exists;
// generating writes its own.
TEST_F(TerrainOutputPanelTest, OffersTheTexturePickerOnlyWhenImporting) {
  textures_.push_back(Texture{.id = "tex-1", .name = "grass_blob47"});

  // The source selector is a combo too, so it needs somewhere to land before
  // the texture picker can be asserted on specifically.
  EXPECT_CALL(gui_, CreateScopedCombo(_, _, _)).Times(::testing::AnyNumber());
  EXPECT_CALL(gui_, CreateScopedCombo(StrEq("Texture##TerrainOut"), _, _)).Times(0);
  ASSERT_OK(Render());
  ::testing::Mock::VerifyAndClearExpectations(&gui_);

  ON_CALL(gui_, CreateScopedCombo(_, _, _))
      .WillByDefault(Invoke([this](const char* label, const char* preview, ImGuiComboFlags) {
        return ScopedCombo(&gui_, label, preview);
      }));
  model_.SetSource(TerrainEditorModel::Source::kImportManifest);
  EXPECT_CALL(gui_, CreateScopedCombo(_, _, _)).Times(::testing::AnyNumber());
  EXPECT_CALL(gui_, CreateScopedCombo(StrEq("Texture##TerrainOut"), _, _)).Times(1);
  ASSERT_OK(Render());
}

// Quality only affects the written artwork, which is why it sits here rather
// than with the controls that change the picture.
TEST_F(TerrainOutputPanelTest, OffersQualityOnlyWhenGenerating) {
  EXPECT_CALL(gui_, SliderInt(StrEq("Quality##TerrainOut"), _, _, _, _, _)).Times(1);
  ASSERT_OK(Render());

  model_.SetSource(TerrainEditorModel::Source::kImportManifest);
  EXPECT_CALL(gui_, SliderInt(StrEq("Quality##TerrainOut"), _, _, _, _, _)).Times(0);
  ASSERT_OK(Render());
}

TEST_F(TerrainOutputPanelTest, LoadedRecipeRegeneratesInsteadOfCreatingNewIds) {
  LoadRecipe();
  ON_CALL(gui_, Button(StrEq("Regenerate##TerrainOut"), _)).WillByDefault(Return(true));

  const absl::StatusOr<TerrainOutputPanel::Action> action = Render();
  ASSERT_OK(action);
  EXPECT_EQ(*action, TerrainOutputPanel::Action::kRegenerate);
}

TEST_F(TerrainOutputPanelTest, SaveAsTurnsALoadedRecipeIntoACopyAction) {
  LoadRecipe();
  ON_CALL(gui_, Button(StrEq("Save As##TerrainOut"), _)).WillByDefault(Return(true));

  const absl::StatusOr<TerrainOutputPanel::Action> action = Render();
  ASSERT_OK(action);
  EXPECT_EQ(*action, TerrainOutputPanel::Action::kCopyRecipe);
}

// Nothing has been produced yet, so there is nothing to delete -- and a Delete
// beside an unsaved configuration would read as discarding the tuning.
TEST_F(TerrainOutputPanelTest, OffersNoDeleteUntilARecipeIsOpen) {
  std::vector<std::string> labels;
  ON_CALL(gui_, Button(_, _)).WillByDefault(Invoke([&labels](const char* label, const ImVec2&) {
    labels.push_back(label);
    return false;
  }));

  ASSERT_OK(Render());
  EXPECT_THAT(labels, Not(Contains("Delete##TerrainOut")));
}

TEST_F(TerrainOutputPanelTest, DeletingAsksBeforeItReportsAnything) {
  LoadRecipe();
  ON_CALL(gui_, Button(StrEq("Delete##TerrainOut"), _)).WillByDefault(Return(true));

  // Pressing Delete raises the question and nothing else: the caller must not
  // destroy three assets on the first click.
  const absl::StatusOr<TerrainOutputPanel::Action> action = Render();
  ASSERT_OK(action);
  EXPECT_EQ(*action, TerrainOutputPanel::Action::kNone);
}

// Three files go, so the question has to say so. Naming only the terrain would
// understate what the click destroys.
TEST_F(TerrainOutputPanelTest, TheDeleteQuestionNamesAllThreeAssets) {
  LoadRecipe();
  ON_CALL(gui_, Button(StrEq("Delete##TerrainOut"), _)).WillByDefault(Return(true));
  ASSERT_OK(Render());

  ON_CALL(gui_, Button(StrEq("Delete##TerrainOut"), _)).WillByDefault(Return(false));
  gui_.ClearWrappedText();
  ASSERT_OK(Render());

  EXPECT_THAT(gui_.wrapped_text(),
              Contains("Delete terrain 'Meadow', its tileset, and its artwork?"));
}

TEST_F(TerrainOutputPanelTest, ConfirmingDeleteReportsTheBundleAction) {
  LoadRecipe();
  ON_CALL(gui_, Button(StrEq("Delete##TerrainOut"), _)).WillByDefault(Return(true));
  ASSERT_OK(Render());

  ON_CALL(gui_, Button(StrEq("Delete##TerrainOut"), _)).WillByDefault(Return(false));
  ON_CALL(gui_, Button(StrEq("Confirm##TerrainOut"), _)).WillByDefault(Return(true));

  const absl::StatusOr<TerrainOutputPanel::Action> action = Render();
  ASSERT_OK(action);
  EXPECT_EQ(*action, TerrainOutputPanel::Action::kDeleteTerrain);
}

TEST_F(TerrainOutputPanelTest, OpeningADifferentRecipeDropsAPendingDeleteQuestion) {
  LoadRecipe();
  ON_CALL(gui_, Button(StrEq("Delete##TerrainOut"), _)).WillByDefault(Return(true));
  ASSERT_OK(Render());

  // The question was armed against 'Meadow'. Answering it after the selection
  // moved would delete whatever is open now.
  model_.LoadRecipe(TerrainRecipe{.id = "recipe-2",
                                  .name = "Cavern",
                                  .tileset_id = "tileset-2",
                                  .texture_id = "texture-2",
                                  .terrain_id = 2});
  ON_CALL(gui_, Button(StrEq("Delete##TerrainOut"), _)).WillByDefault(Return(false));
  ON_CALL(gui_, Button(StrEq("Confirm##TerrainOut"), _)).WillByDefault(Return(true));

  const absl::StatusOr<TerrainOutputPanel::Action> action = Render();
  ASSERT_OK(action);
  EXPECT_EQ(*action, TerrainOutputPanel::Action::kNone);
}

}  // namespace
}  // namespace zebes
