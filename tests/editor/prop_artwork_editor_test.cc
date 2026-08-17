#include "editor/prop_artwork_editor/prop_artwork_editor.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/image_digest.h"
#include "common/image_io.h"
#include "common/utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"
#include "tests/macros.h"

namespace zebes {

class PropArtworkEditorTestPeer {
 public:
  static PropArtworkEditorModel& Model(PropArtworkEditor& editor) { return editor.model_; }
  static void StartPreparation(PropArtworkEditor& editor) { editor.StartPreparation(); }
  static void StartImport(PropArtworkEditor& editor, std::string path) {
    editor.StartImport(std::move(path));
  }
  static void CommitPrepared(PropArtworkEditor& editor) { editor.CommitPrepared(); }
  static void DeleteProp(PropArtworkEditor& editor) { editor.DeleteProp(); }
  static void PollWork(PropArtworkEditor& editor) { editor.PollWork(); }
  static bool HasPendingWork(const PropArtworkEditor& editor) { return editor.HasPendingWork(); }
  static absl::Status WaitForWork(PropArtworkEditor& editor) {
    if (auto* pending = std::get_if<PropArtworkEditor::PendingCreation>(&editor.pending_work_);
        pending != nullptr) {
      return pending->work.Wait();
    }
    if (auto* pending = std::get_if<PropArtworkEditor::PendingRegeneration>(&editor.pending_work_);
        pending != nullptr) {
      return pending->work.Wait();
    }
    if (auto* pending = std::get_if<PropArtworkEditor::PendingImport>(&editor.pending_work_);
        pending != nullptr) {
      return pending->work.Wait();
    }
    return absl::FailedPreconditionError("No prop artwork work is pending");
  }
};

namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

class StubPreviewSink : public PreviewTextureSink {
 public:
  absl::StatusOr<ImTextureID> Upload(const RgbaImage&) override { return ImTextureID{0}; }
};

RgbaImage SourcePixels() {
  RgbaImage image{.width = 32, .height = 24};
  image.pixels.assign(static_cast<size_t>(image.width) * image.height * 4, 255);
  for (size_t pixel = 0; pixel < static_cast<size_t>(image.width) * image.height; ++pixel) {
    image.pixels[pixel * 4 + 0] = 240;
    image.pixels[pixel * 4 + 1] = 240;
    image.pixels[pixel * 4 + 2] = 240;
  }
  for (int y = 6; y < 20; ++y) {
    for (int x = 7; x < 25; ++x) {
      const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
      image.pixels[offset + 0] = 72;
      image.pixels[offset + 1] = 66;
      image.pixels[offset + 2] = 62;
    }
  }
  return image;
}

class PropArtworkEditorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(editor_, PropArtworkEditor::Create(&api_, &gui_, &preview_));
    pixels_ = SourcePixels();
    ASSERT_OK_AND_ASSIGN(const std::string digest, RgbaImageDigest(pixels_));
    source_ = SourceArtwork{
        .id = "source-1",
        .name = "Boulder source",
        .source_path = "source_art/props/source-1.png",
        .provenance =
            ImportedArtworkProvenance{
                .original_filename = "boulder.png",
                .imported_at_utc = "2026-08-16T15:04:05Z",
            },
        .width = pixels_.width,
        .height = pixels_.height,
        .content_digest = digest,
    };
    terrain_ = TerrainRecipe{
        .id = "terrain-1",
        .name = "Lucinda cave",
        .tileset_id = "tileset-1",
        .texture_id = "terrain-texture-1",
        .terrain_id = 1,
    };
    terrain_.config.tile_size = 8;
    terrain_.config.supersample = 1;

    ASSERT_OK(model().SelectSource(source_, pixels_));
    ASSERT_OK(model().AttachTerrain(terrain_));
    model().name() = "Cave boulder";
    model().settings().pipeline.isolation.minimum_subject_area = 16;
    model().settings().pipeline.composition.canvas_tiles_wide = 2;
    model().settings().pipeline.composition.canvas_tiles_high = 2;
    model().MarkInputsChanged();
  }

  void TearDown() override {
    if (temporary_path_.empty()) return;
    std::error_code ignored;
    std::filesystem::remove(temporary_path_, ignored);
  }

  PropArtworkEditorModel& model() { return PropArtworkEditorTestPeer::Model(*editor_); }

  NiceMock<MockApi> api_;
  NiceMock<MockGui> gui_;
  StubPreviewSink preview_;
  std::unique_ptr<PropArtworkEditor> editor_;
  RgbaImage pixels_;
  SourceArtwork source_;
  TerrainRecipe terrain_;
  std::string temporary_path_;
};

TEST_F(PropArtworkEditorTest, WorkerPreparesWithoutPublishingAndEditorCommitsAfterReview) {
  EXPECT_CALL(api_, CreateGeneratedProp(_)).Times(0);

  PropArtworkEditorTestPeer::StartPreparation(*editor_);
  ASSERT_TRUE(PropArtworkEditorTestPeer::HasPendingWork(*editor_));
  ASSERT_OK(PropArtworkEditorTestPeer::WaitForWork(*editor_));
  PropArtworkEditorTestPeer::PollWork(*editor_);

  EXPECT_FALSE(PropArtworkEditorTestPeer::HasPendingWork(*editor_));
  ASSERT_NE(model().prepared_creation(), nullptr);
  EXPECT_THAT(model().status(), HasSubstr("Review"));
  testing::Mock::VerifyAndClearExpectations(&api_);

  EXPECT_CALL(api_, CreateGeneratedProp(_)).WillOnce(Return(std::string("recipe-1")));
  PropArtworkEditorTestPeer::CommitPrepared(*editor_);

  ASSERT_TRUE(model().active_recipe().has_value());
  EXPECT_EQ(model().active_recipe()->id, model().prepared_creation()->recipe.id);
  EXPECT_THAT(model().status(), HasSubstr("collider-free blueprint"));
}

TEST_F(PropArtworkEditorTest, DeleteUsesTheBundleApiAndClearsTheEditor) {
  PropRecipe recipe{.id = "recipe-1",
                    .name = "Cave boulder",
                    .source_artwork_id = source_.id,
                    .terrain_recipe_id = terrain_.id};
  recipe.style = model().settings().style;
  recipe.pipeline = model().settings().pipeline;
  recipe.texture_id = "texture-1";
  recipe.sprite_id = "sprite-1";
  recipe.blueprint_id = "blueprint-1";
  recipe.expected_frame =
      SpriteFrame{.index = 0, .texture_w = 16, .texture_h = 16, .render_w = 16, .render_h = 16};
  recipe.final_pixel_digest = std::string(64, 'a');
  ASSERT_OK(model().LoadRecipe(recipe, source_, pixels_, terrain_));
  EXPECT_CALL(api_, DeleteGeneratedProp(StrEq("recipe-1"))).WillOnce(Return(absl::OkStatus()));

  PropArtworkEditorTestPeer::DeleteProp(*editor_);

  EXPECT_FALSE(model().active_recipe().has_value());
  EXPECT_FALSE(model().source().has_value());
  EXPECT_THAT(model().status(), HasSubstr("Deleted 'Cave boulder'"));
}

TEST_F(PropArtworkEditorTest, ImportDecodesOnAWorkerThenAcceptsSourceOnTheEditorThread) {
  temporary_path_ = absl::StrCat("/tmp/zebes-prop-import-", GenerateGuid(), ".png");
  ASSERT_OK(WritePng(temporary_path_, pixels_.width, pixels_.height, pixels_.pixels));

  SourceArtwork imported;
  EXPECT_CALL(api_, CreateSourceArtwork(_, _, _))
      .WillOnce([&](std::string name, SourceArtworkProvenance provenance, const RgbaImage& image) {
        absl::StatusOr<std::string> digest = RgbaImageDigest(image);
        EXPECT_TRUE(digest.ok());
        imported = SourceArtwork{
            .id = "imported-source",
            .name = std::move(name),
            .source_path = "source_art/props/imported-source.png",
            .provenance = std::move(provenance),
            .width = image.width,
            .height = image.height,
            .content_digest = digest.ok() ? *digest : std::string(64, '0'),
        };
        return absl::StatusOr<std::string>(imported.id);
      });
  EXPECT_CALL(api_, GetSourceArtwork(StrEq("imported-source"))).WillOnce(Return(&imported));

  PropArtworkEditorTestPeer::StartImport(*editor_, temporary_path_);
  ASSERT_TRUE(PropArtworkEditorTestPeer::HasPendingWork(*editor_));
  ASSERT_OK(PropArtworkEditorTestPeer::WaitForWork(*editor_));
  PropArtworkEditorTestPeer::PollWork(*editor_);

  ASSERT_TRUE(model().source().has_value());
  EXPECT_EQ(model().source()->id, "imported-source");
  EXPECT_THAT(model().status(), HasSubstr("Accepted source"));
}

TEST_F(PropArtworkEditorTest, RegenerationPreparesBeforeCallingTheBundleApi) {
  PropArtworkEditorTestPeer::StartPreparation(*editor_);
  ASSERT_OK(PropArtworkEditorTestPeer::WaitForWork(*editor_));
  PropArtworkEditorTestPeer::PollWork(*editor_);
  ASSERT_NE(model().prepared_creation(), nullptr);

  const PreparedPropAsset created = *model().prepared_creation();
  model().BindCommittedRecipe(created.recipe);
  model().settings().pipeline.edge.width = 0;
  model().MarkInputsChanged();

  Texture texture = created.texture;
  Sprite sprite = created.sprite;
  EXPECT_CALL(api_, GetTexture(StrEq(created.texture.id))).WillOnce(Return(&texture));
  EXPECT_CALL(api_, ReadTexturePixels(StrEq(created.texture.id)))
      .WillOnce(Return(created.artwork.finished.image));
  EXPECT_CALL(api_, GetSprite(StrEq(created.sprite.id))).WillOnce(Return(&sprite));
  EXPECT_CALL(api_, RegenerateGeneratedProp(_)).Times(0);

  PropArtworkEditorTestPeer::StartPreparation(*editor_);
  ASSERT_TRUE(PropArtworkEditorTestPeer::HasPendingWork(*editor_));
  ASSERT_OK(PropArtworkEditorTestPeer::WaitForWork(*editor_));
  PropArtworkEditorTestPeer::PollWork(*editor_);
  ASSERT_NE(model().prepared_regeneration(), nullptr);
  testing::Mock::VerifyAndClearExpectations(&api_);

  EXPECT_CALL(api_, RegenerateGeneratedProp(_)).WillOnce(Return(absl::OkStatus()));
  PropArtworkEditorTestPeer::CommitPrepared(*editor_);

  EXPECT_THAT(model().status(), HasSubstr("without changing asset IDs"));
}

TEST_F(PropArtworkEditorTest, RefusedDeleteKeepsTheRecipeOpen) {
  PropRecipe recipe{.id = "recipe-1",
                    .name = "Cave boulder",
                    .source_artwork_id = source_.id,
                    .terrain_recipe_id = terrain_.id};
  recipe.style = model().settings().style;
  recipe.pipeline = model().settings().pipeline;
  recipe.texture_id = "texture-1";
  recipe.sprite_id = "sprite-1";
  recipe.blueprint_id = "blueprint-1";
  recipe.expected_frame =
      SpriteFrame{.index = 0, .texture_w = 16, .texture_h = 16, .render_w = 16, .render_h = 16};
  recipe.final_pixel_digest = std::string(64, 'a');
  ASSERT_OK(model().LoadRecipe(recipe, source_, pixels_, terrain_));
  EXPECT_CALL(api_, DeleteGeneratedProp(StrEq("recipe-1")))
      .WillOnce(Return(absl::FailedPreconditionError("Level 'Cave' places its blueprint")));

  PropArtworkEditorTestPeer::DeleteProp(*editor_);

  ASSERT_TRUE(model().active_recipe().has_value());
  EXPECT_THAT(model().status(), HasSubstr("Level 'Cave'"));
}

}  // namespace
}  // namespace zebes
