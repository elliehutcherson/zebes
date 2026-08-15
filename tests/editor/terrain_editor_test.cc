#include "editor/terrain_editor/terrain_editor.h"

#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"

namespace zebes {

// Reaches the private action handlers. The panel already has its own tests for
// which button reports what; these are about what the editor does with the
// answer.
class TerrainEditorTestPeer {
 public:
  static void DeleteTerrain(TerrainEditor& editor) { editor.DeleteTerrain(); }
  static TerrainEditorModel& GetModel(TerrainEditor& editor) { return editor.model_; }
};

namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

// The tab never draws during these tests, so nothing is ever uploaded.
class StubPreviewSink : public PreviewTextureSink {
 public:
  absl::StatusOr<ImTextureID> Upload(const RgbaImage&) override { return ImTextureID{0}; }
};

class TerrainEditorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<std::unique_ptr<TerrainEditor>> editor =
        TerrainEditor::Create(&api_, &gui_, &preview_);
    ASSERT_OK(editor);
    editor_ = *std::move(editor);
  }

  TerrainEditorModel& model() { return TerrainEditorTestPeer::GetModel(*editor_); }

  void OpenRecipe() {
    model().LoadRecipe(TerrainRecipe{.id = "recipe-1",
                                     .name = "Meadow",
                                     .tileset_id = "tileset-1",
                                     .texture_id = "texture-1",
                                     .terrain_id = 1});
  }

  NiceMock<MockApi> api_;
  NiceMock<MockGui> gui_;
  StubPreviewSink preview_;
  std::unique_ptr<TerrainEditor> editor_;
};

TEST_F(TerrainEditorTest, DeletingWithNothingOpenAsksForARecipeRatherThanFailing) {
  EXPECT_CALL(api_, DeleteGeneratedTerrain(_)).Times(0);

  TerrainEditorTestPeer::DeleteTerrain(*editor_);

  EXPECT_THAT(model().status(), HasSubstr("Open a terrain recipe"));
}

TEST_F(TerrainEditorTest, DeletingRemovesTheOpenRecipeWholeAndEmptiesTheTab) {
  OpenRecipe();
  EXPECT_CALL(api_, DeleteGeneratedTerrain(StrEq("recipe-1"))).WillOnce(Return(absl::OkStatus()));

  TerrainEditorTestPeer::DeleteTerrain(*editor_);

  // Nothing is open, and no result is left claiming assets that are gone.
  // Leaving the binding would let Regenerate write a tileset back out.
  EXPECT_FALSE(model().active_recipe().has_value());
  EXPECT_FALSE(model().result().has_value());
  EXPECT_THAT(model().status(), HasSubstr("Meadow"));
}

// A refusal names every referrer. Showing it as the whole message rather than
// appending it to one keeps the list readable, and the terrain stays open so
// the user can act on what it named.
TEST_F(TerrainEditorTest, ARefusedDeleteLeavesTheTerrainOpenAndSaysWhatBlockedIt) {
  OpenRecipe();
  EXPECT_CALL(api_, DeleteGeneratedTerrain(StrEq("recipe-1")))
      .WillOnce(Return(absl::FailedPreconditionError(
          "Cannot delete terrain 'Meadow'. 1 thing references it:\n  Level 'Donut Plains' "
          "(tileset_id)")));

  TerrainEditorTestPeer::DeleteTerrain(*editor_);

  ASSERT_TRUE(model().active_recipe().has_value());
  EXPECT_EQ(model().active_recipe()->id, "recipe-1");
  EXPECT_THAT(model().status(), HasSubstr("Level 'Donut Plains' (tileset_id)"));
}

}  // namespace
}  // namespace zebes
