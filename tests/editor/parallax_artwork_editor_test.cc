#include "editor/parallax_artwork_editor/parallax_artwork_editor.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/image_digest.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"
#include "tests/macros.h"

namespace zebes {

class ParallaxArtworkEditorTestPeer {
 public:
  static ParallaxArtworkEditorModel& Model(ParallaxArtworkEditor& editor) { return editor.model_; }
  static void StartPreparation(ParallaxArtworkEditor& editor) { editor.StartPreparation(); }
  static void StartImport(ParallaxArtworkEditor& editor, std::string path) {
    editor.StartImport(std::move(path));
  }
  static void CommitPrepared(ParallaxArtworkEditor& editor) { editor.CommitPrepared(); }
  static void ClearWorkspace(ParallaxArtworkEditor& editor) { editor.ClearWorkspace(); }
  static void DeleteArtwork(ParallaxArtworkEditor& editor) { editor.DeleteArtwork(); }
  static void PollWork(ParallaxArtworkEditor& editor) { editor.PollWork(); }
  static bool HasPendingWork(const ParallaxArtworkEditor& editor) {
    return editor.HasPendingWork();
  }
  static bool HasSessionSource(const ParallaxArtworkEditor& editor) {
    return editor.session_source_id_.has_value();
  }
  static void OwnSessionSource(ParallaxArtworkEditor& editor, std::string id) {
    editor.session_source_id_ = std::move(id);
  }
  static absl::Status WaitForWork(ParallaxArtworkEditor& editor) {
    if (auto* pending = std::get_if<ParallaxArtworkEditor::PendingCreation>(&editor.pending_work_);
        pending != nullptr) {
      return pending->work.Wait();
    }
    if (auto* pending =
            std::get_if<ParallaxArtworkEditor::PendingRegeneration>(&editor.pending_work_);
        pending != nullptr) {
      return pending->work.Wait();
    }
    if (auto* pending = std::get_if<ParallaxArtworkEditor::PendingImport>(&editor.pending_work_);
        pending != nullptr) {
      return pending->work.Wait();
    }
    return absl::FailedPreconditionError("No parallax artwork work is pending");
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
  RgbaImage image{.width = 12, .height = 8};
  image.pixels.assign(static_cast<size_t>(image.width) * image.height * 4, 255);
  for (size_t pixel = 0; pixel < static_cast<size_t>(image.width) * image.height; ++pixel) {
    image.pixels[pixel * 4 + 0] = 42;
    image.pixels[pixel * 4 + 1] = 52;
    image.pixels[pixel * 4 + 2] = 62;
  }
  return image;
}

class ParallaxArtworkEditorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(editor_, ParallaxArtworkEditor::Create({
                                      .api = &api_,
                                      .gui = &gui_,
                                      .preview = &preview_,
                                  }));
    pixels_ = SourcePixels();
    ASSERT_OK_AND_ASSIGN(const std::string digest, RgbaImageDigest(pixels_));
    source_ = SourceArtwork{
        .id = "source-1",
        .name = "Cave plate source",
        .source_path = "source_art/source-1.png",
        .provenance =
            ImportedArtworkProvenance{
                .original_filename = "cave-plate.png",
                .imported_at_utc = "2026-08-23T12:00:00Z",
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
    model().name() = "Cave far fill";
    model().settings().pipeline.target_width = pixels_.width;
    model().settings().pipeline.target_height = pixels_.height;
    model().MarkInputsChanged();
  }

  void TearDown() override {
    if (temporary_path_.empty()) return;
    std::error_code ignored;
    std::filesystem::remove(temporary_path_, ignored);
  }

  ParallaxArtworkEditorModel& model() { return ParallaxArtworkEditorTestPeer::Model(*editor_); }

  NiceMock<MockApi> api_;
  NiceMock<MockGui> gui_;
  StubPreviewSink preview_;
  std::unique_ptr<ParallaxArtworkEditor> editor_;
  RgbaImage pixels_;
  SourceArtwork source_;
  TerrainRecipe terrain_;
  std::string temporary_path_;
};

TEST_F(ParallaxArtworkEditorTest, WorkerPreparesBeforeEditorPublishesTheBundle) {
  ParallaxArtworkEditorTestPeer::OwnSessionSource(*editor_, source_.id);
  EXPECT_CALL(api_, CreateGeneratedParallaxArtwork(_)).Times(0);
  EXPECT_CALL(api_, DeleteSourceArtwork(_)).Times(0);

  ParallaxArtworkEditorTestPeer::StartPreparation(*editor_);
  ASSERT_TRUE(ParallaxArtworkEditorTestPeer::HasPendingWork(*editor_));
  ASSERT_OK(ParallaxArtworkEditorTestPeer::WaitForWork(*editor_));
  ParallaxArtworkEditorTestPeer::PollWork(*editor_);

  ASSERT_NE(model().prepared_creation(), nullptr);
  EXPECT_THAT(model().status(), HasSubstr("Review"));
  EXPECT_EQ(model().status_kind(), ParallaxArtworkStatusKind::kReady);
  EXPECT_TRUE(model().HasUncommittedPreparedResult());
  testing::Mock::VerifyAndClearExpectations(&api_);

  EXPECT_CALL(api_, CreateGeneratedParallaxArtwork(_)).WillOnce(Return(std::string("recipe-1")));
  ParallaxArtworkEditorTestPeer::CommitPrepared(*editor_);

  ASSERT_TRUE(model().active_recipe().has_value());
  EXPECT_FALSE(ParallaxArtworkEditorTestPeer::HasSessionSource(*editor_));
  EXPECT_THAT(model().status(), HasSubstr("managed texture"));
  EXPECT_EQ(model().status_kind(), ParallaxArtworkStatusKind::kSuccess);
  EXPECT_FALSE(model().HasUncommittedPreparedResult());
}

TEST_F(ParallaxArtworkEditorTest, FailedCreationIsClearlyReportedAndRemainsRetryable) {
  ParallaxArtworkEditorTestPeer::StartPreparation(*editor_);
  ASSERT_OK(ParallaxArtworkEditorTestPeer::WaitForWork(*editor_));
  ParallaxArtworkEditorTestPeer::PollWork(*editor_);
  ASSERT_TRUE(model().HasUncommittedPreparedResult());
  EXPECT_CALL(api_, CreateGeneratedParallaxArtwork(_))
      .WillOnce(Return(absl::InvalidArgumentError("invalid generated texture path")));

  ParallaxArtworkEditorTestPeer::CommitPrepared(*editor_);

  EXPECT_EQ(model().status_kind(), ParallaxArtworkStatusKind::kError);
  EXPECT_THAT(model().status(), HasSubstr("Create artwork failed"));
  EXPECT_THAT(model().status(), HasSubstr("invalid generated texture path"));
  EXPECT_FALSE(model().active_recipe().has_value());
  EXPECT_TRUE(model().HasUncommittedPreparedResult());
}

TEST_F(ParallaxArtworkEditorTest, ImportRetainsOnTheEditorThreadAndClearCompensates) {
  temporary_path_ = absl::StrCat("/tmp/zebes-parallax-import-", GenerateGuid(), ".png");
  ASSERT_OK(WritePng(temporary_path_, pixels_.width, pixels_.height, pixels_.pixels));

  SourceArtwork imported;
  EXPECT_CALL(api_, CreateSourceArtwork(_, _, _))
      .WillOnce([&](std::string name, SourceArtworkProvenance provenance, const RgbaImage& image) {
        absl::StatusOr<std::string> digest = RgbaImageDigest(image);
        EXPECT_TRUE(digest.ok());
        imported = SourceArtwork{
            .id = "imported-source",
            .name = std::move(name),
            .source_path = "source_art/imported-source.png",
            .provenance = std::move(provenance),
            .width = image.width,
            .height = image.height,
            .content_digest = digest.ok() ? *digest : std::string(64, '0'),
        };
        return absl::StatusOr<std::string>(imported.id);
      });
  EXPECT_CALL(api_, GetSourceArtwork(StrEq("imported-source"))).WillOnce(Return(&imported));

  ParallaxArtworkEditorTestPeer::StartImport(*editor_, temporary_path_);
  ASSERT_OK(ParallaxArtworkEditorTestPeer::WaitForWork(*editor_));
  ParallaxArtworkEditorTestPeer::PollWork(*editor_);

  ASSERT_TRUE(model().source().has_value());
  EXPECT_EQ(model().source()->id, imported.id);
  EXPECT_TRUE(ParallaxArtworkEditorTestPeer::HasSessionSource(*editor_));

  EXPECT_CALL(api_, DeleteSourceArtwork(StrEq(imported.id))).WillOnce(Return(absl::OkStatus()));
  ParallaxArtworkEditorTestPeer::ClearWorkspace(*editor_);

  EXPECT_FALSE(model().source().has_value());
  EXPECT_FALSE(ParallaxArtworkEditorTestPeer::HasSessionSource(*editor_));
}

TEST_F(ParallaxArtworkEditorTest, RegenerationKeepsRecipeAndTextureIdentity) {
  ParallaxArtworkEditorTestPeer::StartPreparation(*editor_);
  ASSERT_OK(ParallaxArtworkEditorTestPeer::WaitForWork(*editor_));
  ParallaxArtworkEditorTestPeer::PollWork(*editor_);
  ASSERT_NE(model().prepared_creation(), nullptr);
  const PreparedParallaxArtworkAsset created = *model().prepared_creation();
  model().BindCommittedRecipe(created.recipe);
  model().settings().pipeline.review_repeat_x = true;
  model().MarkInputsChanged();

  Texture texture = created.texture;
  EXPECT_CALL(api_, GetTexture(StrEq(created.texture.id))).WillOnce(Return(&texture));
  EXPECT_CALL(api_, ReadTexturePixels(StrEq(created.texture.id)))
      .WillOnce(Return(created.artwork.finished));
  EXPECT_CALL(api_, RegenerateGeneratedParallaxArtwork(_)).Times(0);

  ParallaxArtworkEditorTestPeer::StartPreparation(*editor_);
  ASSERT_OK(ParallaxArtworkEditorTestPeer::WaitForWork(*editor_));
  ParallaxArtworkEditorTestPeer::PollWork(*editor_);
  ASSERT_NE(model().prepared_regeneration(), nullptr);
  testing::Mock::VerifyAndClearExpectations(&api_);

  EXPECT_CALL(api_, RegenerateGeneratedParallaxArtwork(_)).WillOnce(Return(absl::OkStatus()));
  ParallaxArtworkEditorTestPeer::CommitPrepared(*editor_);

  ASSERT_TRUE(model().active_recipe().has_value());
  EXPECT_EQ(model().active_recipe()->id, created.recipe.id);
  EXPECT_EQ(model().active_recipe()->texture_id, created.texture.id);
  EXPECT_THAT(model().status(), HasSubstr("without changing"));
}

TEST_F(ParallaxArtworkEditorTest, RefusedDeleteKeepsTheRecipeOpen) {
  ParallaxArtworkEditorTestPeer::StartPreparation(*editor_);
  ASSERT_OK(ParallaxArtworkEditorTestPeer::WaitForWork(*editor_));
  ParallaxArtworkEditorTestPeer::PollWork(*editor_);
  ASSERT_NE(model().prepared_creation(), nullptr);
  model().BindCommittedRecipe(model().prepared_creation()->recipe);
  EXPECT_CALL(api_, DeleteGeneratedParallaxArtwork(StrEq(model().active_recipe()->id)))
      .WillOnce(Return(absl::FailedPreconditionError("Cave theme uses its texture")));

  ParallaxArtworkEditorTestPeer::DeleteArtwork(*editor_);

  EXPECT_TRUE(model().active_recipe().has_value());
  EXPECT_THAT(model().status(), HasSubstr("Cave theme"));
}

}  // namespace
}  // namespace zebes
