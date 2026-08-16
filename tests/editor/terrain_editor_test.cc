#include "editor/terrain_editor/terrain_editor.h"

#include <memory>
#include <string>

#include "absl/status/status.h"
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
  static void CreateTerrain(TerrainEditor& editor) { editor.CreateTerrain(); }
  static void RegenerateTerrain(TerrainEditor& editor) { editor.RegenerateTerrain(); }
  static void PollTerrainWork(TerrainEditor& editor) { editor.PollTerrainWork(); }
  static bool HasPendingTerrainWork(const TerrainEditor& editor) {
    return editor.HasPendingTerrainWork();
  }
  static absl::Status WaitForTerrainWork(TerrainEditor& editor) {
    if (auto* pending = std::get_if<TerrainEditor::PendingCreation>(&editor.pending_work_);
        pending != nullptr) {
      return pending->work.Wait();
    }
    if (auto* pending = std::get_if<TerrainEditor::PendingRegeneration>(&editor.pending_work_);
        pending != nullptr) {
      return pending->work.Wait();
    }
    return absl::FailedPreconditionError("No terrain work is pending");
  }
  static void DeleteTerrain(TerrainEditor& editor) { editor.DeleteTerrain(); }
  static TerrainEditorModel& GetModel(TerrainEditor& editor) { return editor.model_; }
};

namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Mock;
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

TEST_F(TerrainEditorTest, GeneratedArtworkCommitsOnlyWhenTheEditorPollsTheWorker) {
  model().name() = "meadow";
  model().config().tile_size = 8;
  model().config().supersample = 1;
  model().config().variant_period = 1;

  // The worker owns only copied generator input. Even after it finishes, Api
  // remains untouched until the editor thread polls and commits the result.
  EXPECT_CALL(api_, CreateTextureFromPixels(_, _, _, _)).Times(0);
  EXPECT_CALL(api_, CreateTileset(_)).Times(0);
  EXPECT_CALL(api_, CreateTerrainRecipe(_)).Times(0);

  TerrainEditorTestPeer::CreateTerrain(*editor_);
  EXPECT_TRUE(TerrainEditorTestPeer::HasPendingTerrainWork(*editor_));
  EXPECT_THAT(model().status(), HasSubstr("background"));
  ASSERT_OK(TerrainEditorTestPeer::WaitForTerrainWork(*editor_));
  Mock::VerifyAndClearExpectations(&api_);

  EXPECT_CALL(api_, CreateTextureFromPixels("meadow", _, _, _))
      .WillOnce(Return(std::string("texture-id")));
  EXPECT_CALL(api_, CreateTileset(_)).WillOnce(Return(std::string("tileset-id")));
  EXPECT_CALL(api_, CreateTerrainRecipe(_)).WillOnce(Return(std::string("recipe-id")));
  EXPECT_CALL(api_, GetTerrainRecipe("recipe-id"))
      .WillOnce(Return(absl::NotFoundError("not cached in this mock")));

  TerrainEditorTestPeer::PollTerrainWork(*editor_);

  EXPECT_FALSE(TerrainEditorTestPeer::HasPendingTerrainWork(*editor_));
  ASSERT_TRUE(model().result().has_value());
  EXPECT_EQ(model().result()->texture_id, "texture-id");
  EXPECT_EQ(model().result()->tileset_id, "tileset-id");
  EXPECT_EQ(model().result()->recipe_id, "recipe-id");
  EXPECT_THAT(model().status(), HasSubstr("Created 'meadow'"));
}

TEST_F(TerrainEditorTest, RegeneratedArtworkAlsoCommitsOnlyWhenPolled) {
  TerrainGenConfig config;
  config.tile_size = 8;
  config.supersample = 1;
  config.variant_period = 1;
  TerrainRecipe recipe{.id = "recipe-id",
                       .name = "meadow",
                       .tileset_id = "tileset-id",
                       .texture_id = "texture-id",
                       .terrain_id = 1,
                       .config = config};
  model().LoadRecipe(recipe);

  Tileset tileset{.id = "tileset-id",
                  .name = "meadow",
                  .texture_id = "texture-id",
                  .tile_width = 8,
                  .tile_height = 8};
  tileset.tiles.push_back(Tile{.id = 1, .name = "block", .shape = TileShape::kFullBlock});
  TerrainCellKey key;
  key.shape = TileShape::kFullBlock;
  key.neighbors.fill(TileShape::kFullBlock);
  tileset.terrains.push_back(Terrain{.id = 1,
                                     .name = "meadow",
                                     .scheme = TerrainScheme::kDerived,
                                     .variant_period = 1,
                                     .derived_tiles = {{.tile_id = 1, .key = key}}});

  EXPECT_CALL(api_, GetTileset("tileset-id")).WillOnce(Return(&tileset));
  EXPECT_CALL(api_, SaveTerrainRecipe(_)).Times(0);
  EXPECT_CALL(api_, ReplaceTexturePixels(_, _, _, _)).Times(0);

  TerrainEditorTestPeer::RegenerateTerrain(*editor_);
  EXPECT_TRUE(TerrainEditorTestPeer::HasPendingTerrainWork(*editor_));
  ASSERT_OK(TerrainEditorTestPeer::WaitForTerrainWork(*editor_));
  Mock::VerifyAndClearExpectations(&api_);

  EXPECT_CALL(api_, GetTileset("tileset-id")).WillOnce(Return(&tileset));
  EXPECT_CALL(api_, SaveTerrainRecipe(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(api_, ReplaceTexturePixels("texture-id", _, _, _)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(api_, GetTerrainRecipe("recipe-id"))
      .WillOnce(Return(absl::NotFoundError("not cached in this mock")));

  TerrainEditorTestPeer::PollTerrainWork(*editor_);

  EXPECT_FALSE(TerrainEditorTestPeer::HasPendingTerrainWork(*editor_));
  EXPECT_THAT(model().status(), HasSubstr("Regenerated 'meadow'"));
}

}  // namespace
}  // namespace zebes
