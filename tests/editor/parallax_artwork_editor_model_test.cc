#include "editor/parallax_artwork_editor/parallax_artwork_editor_model.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "artwork/prepare_parallax_artwork_asset.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "gtest/gtest.h"
#include "terrain/terrain_palette.h"
#include "tests/macros.h"

namespace zebes {
namespace {

RgbaImage SourcePixels() {
  RgbaImage image{.width = 12, .height = 8};
  image.pixels.assign(static_cast<size_t>(image.width) * image.height * 4, 255);
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
      image.pixels[offset + 0] = static_cast<uint8_t>(30 + x);
      image.pixels[offset + 1] = static_cast<uint8_t>(40 + y);
      image.pixels[offset + 2] = 50;
    }
  }
  return image;
}

absl::StatusOr<SourceArtwork> AcceptedSource(const RgbaImage& pixels) {
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(pixels));
  return SourceArtwork{
      .id = "source-1",
      .name = "Cave plate source",
      .source_path = "source_art/source-1.png",
      .provenance =
          ImportedArtworkProvenance{
              .original_filename = "cave-plate.png",
              .imported_at_utc = "2026-08-23T12:00:00Z",
          },
      .width = pixels.width,
      .height = pixels.height,
      .content_digest = digest,
  };
}

TerrainRecipe Terrain() {
  TerrainRecipe recipe{
      .id = "terrain-1",
      .name = "Lucinda cave",
      .tileset_id = "tileset-1",
      .texture_id = "terrain-texture-1",
      .terrain_id = 1,
  };
  recipe.config.tile_size = 8;
  recipe.config.supersample = 1;
  return recipe;
}

absl::StatusOr<PreparedParallaxArtworkAsset> PrepareFor(const ParallaxArtworkEditorModel& model) {
  return PrepareParallaxArtworkAsset(
      *model.source(), *model.source_pixels(),
      PrepareParallaxArtworkAssetRequest{
          .name = model.name(),
          .terrain_recipe_id = model.settings().terrain_recipe_id,
          .style = model.settings().style,
          .pipeline = model.settings().pipeline,
          .ids = {.texture_id = "texture-1", .recipe_id = "recipe-1"},
      });
}

class ParallaxArtworkEditorModelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    pixels_ = SourcePixels();
    ASSERT_OK_AND_ASSIGN(source_, AcceptedSource(pixels_));
    ASSERT_OK(model_.SelectSource(source_, pixels_));
    ASSERT_OK(model_.AttachTerrain(terrain_));
    model_.settings().pipeline.target_width = 12;
    model_.settings().pipeline.target_height = 8;
    model_.MarkInputsChanged();
  }

  RgbaImage pixels_;
  SourceArtwork source_;
  TerrainRecipe terrain_ = Terrain();
  ParallaxArtworkEditorModel model_;
};

TEST_F(ParallaxArtworkEditorModelTest, AttachingTerrainCapturesItsResolvedPalette) {
  ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette expected,
                       ResolveTerrainPalette(terrain_.config));
  EXPECT_EQ(model_.settings().terrain_recipe_id, terrain_.id);
  EXPECT_EQ(model_.settings().style.palette, expected.OpaqueColors());
  EXPECT_TRUE(model_.settings().style.quantize_to_palette);
}

TEST_F(ParallaxArtworkEditorModelTest, SupersededWorkerResultIsRejected) {
  const uint64_t prepared_revision = model_.revision();
  ASSERT_OK_AND_ASSIGN(PreparedParallaxArtworkAsset prepared, PrepareFor(model_));
  model_.settings().pipeline.target_width = 16;
  model_.MarkInputsChanged();

  EXPECT_EQ(model_.AcceptPrepared(prepared_revision, std::move(prepared)).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_FALSE(model_.HasPreparedResult());
}

TEST_F(ParallaxArtworkEditorModelTest, PreviewVisitsPipelineAndRequestedRepeatStages) {
  model_.settings().pipeline.review_repeat_x = true;
  model_.settings().pipeline.review_repeat_y = true;
  model_.MarkInputsChanged();
  ASSERT_OK_AND_ASSIGN(PreparedParallaxArtworkAsset prepared, PrepareFor(model_));
  ASSERT_OK(model_.AcceptPrepared(model_.revision(), std::move(prepared)));

  model_.SetPreviewStage(ParallaxArtworkPreviewStage::kSource);
  const std::vector<ParallaxArtworkPreviewStage> expected = {
      ParallaxArtworkPreviewStage::kSource,          ParallaxArtworkPreviewStage::kFraming,
      ParallaxArtworkPreviewStage::kMatteExtraction, ParallaxArtworkPreviewStage::kRasterization,
      ParallaxArtworkPreviewStage::kFinished,        ParallaxArtworkPreviewStage::kRepeatX,
      ParallaxArtworkPreviewStage::kRepeatY,
  };
  for (const ParallaxArtworkPreviewStage stage : expected) {
    EXPECT_EQ(model_.preview_stage(), stage);
    EXPECT_NE(model_.PreviewImage(), nullptr);
    model_.NextPreviewStage();
  }
  EXPECT_NE(model_.repetition_diagnostics(), nullptr);
}

TEST_F(ParallaxArtworkEditorModelTest, CommittedPreviewCannotBePublishedAgain) {
  ASSERT_OK_AND_ASSIGN(PreparedParallaxArtworkAsset prepared, PrepareFor(model_));
  const ParallaxArtworkRecipe recipe = prepared.recipe;
  ASSERT_OK(model_.AcceptPrepared(model_.revision(), std::move(prepared)));
  EXPECT_TRUE(model_.HasUncommittedPreparedResult());

  model_.BindCommittedRecipe(recipe);

  EXPECT_TRUE(model_.HasPreparedResult());
  EXPECT_FALSE(model_.HasUncommittedPreparedResult());
  model_.settings().pipeline.review_repeat_x = true;
  model_.MarkInputsChanged();
  EXPECT_FALSE(model_.HasPreparedResult());
}

TEST_F(ParallaxArtworkEditorModelTest, ExistingRecipeKeepsItsSourceUntilSaveAs) {
  ASSERT_OK_AND_ASSIGN(PreparedParallaxArtworkAsset prepared, PrepareFor(model_));
  ASSERT_OK(model_.LoadRecipe(prepared.recipe, source_, pixels_, terrain_));

  SourceArtwork another = source_;
  another.id = "source-2";
  another.source_path = "source_art/source-2.png";
  EXPECT_EQ(model_.SelectSource(std::move(another), pixels_).code(),
            absl::StatusCode::kFailedPrecondition);

  model_.StartRecipeCopy();
  EXPECT_FALSE(model_.active_recipe().has_value());
  EXPECT_EQ(model_.source()->id, source_.id);
  EXPECT_FALSE(model_.HasPreparedResult());
}

TEST_F(ParallaxArtworkEditorModelTest, LoadingDetachedRecipeKeepsItsStyleSnapshot) {
  ASSERT_OK_AND_ASSIGN(PreparedParallaxArtworkAsset prepared, PrepareFor(model_));
  prepared.recipe.terrain_recipe_id.reset();
  ASSERT_OK(model_.LoadRecipe(prepared.recipe, source_, pixels_, std::nullopt));

  EXPECT_FALSE(model_.terrain_recipe().has_value());
  EXPECT_FALSE(model_.settings().terrain_recipe_id.has_value());
  EXPECT_TRUE(model_.has_style());
  EXPECT_EQ(model_.settings().style.palette, prepared.recipe.style.palette);
}

TEST_F(ParallaxArtworkEditorModelTest, LoadingRecipeRestoresItsProcessingSettings) {
  ParallaxArtworkPipelineConfig& pipeline = model_.settings().pipeline;
  pipeline.frame_policy = ParallaxArtworkFramePolicy::kFitInside;
  pipeline.alpha_role = ParallaxArtworkAlphaRole::kTransparentOverlay;
  pipeline.overlay_extraction = ParallaxArtworkOverlayExtraction::kRemoveSolidMatte;
  pipeline.overlay_alpha_policy = ParallaxArtworkOverlayAlphaPolicy::kBinary;
  pipeline.matte_color = {30, 40, 50, 255};
  pipeline.matte_transparent_distance = 0.0f;
  pipeline.matte_opaque_distance = 1.0f;
  pipeline.binary_alpha_threshold = 96;
  pipeline.review_repeat_x = true;
  pipeline.review_repeat_y = true;
  model_.MarkInputsChanged();
  ASSERT_OK_AND_ASSIGN(PreparedParallaxArtworkAsset prepared, PrepareFor(model_));

  model_.StartNewRecipe();
  ASSERT_OK(model_.LoadRecipe(prepared.recipe, source_, pixels_, terrain_));

  const ParallaxArtworkPipelineConfig& restored = model_.settings().pipeline;
  EXPECT_EQ(restored.target_width, 12);
  EXPECT_EQ(restored.target_height, 8);
  EXPECT_EQ(restored.frame_policy, ParallaxArtworkFramePolicy::kFitInside);
  EXPECT_EQ(restored.alpha_role, ParallaxArtworkAlphaRole::kTransparentOverlay);
  EXPECT_EQ(restored.overlay_extraction, ParallaxArtworkOverlayExtraction::kRemoveSolidMatte);
  EXPECT_EQ(restored.overlay_alpha_policy, ParallaxArtworkOverlayAlphaPolicy::kBinary);
  EXPECT_EQ(restored.matte_color, (RgbaColor{30, 40, 50, 255}));
  EXPECT_FLOAT_EQ(restored.matte_transparent_distance, 0.0f);
  EXPECT_FLOAT_EQ(restored.matte_opaque_distance, 1.0f);
  EXPECT_EQ(restored.binary_alpha_threshold, 96);
  EXPECT_TRUE(restored.review_repeat_x);
  EXPECT_TRUE(restored.review_repeat_y);
  EXPECT_EQ(model_.settings().style.palette, prepared.recipe.style.palette);
}

}  // namespace
}  // namespace zebes
