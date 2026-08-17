#include "editor/prop_artwork_editor/prop_artwork_editor_model.h"

#include <cstddef>
#include <string>

#include "artwork/prepare_prop_asset.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "gtest/gtest.h"
#include "terrain/terrain_palette.h"
#include "tests/macros.h"

namespace zebes {
namespace {

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

absl::StatusOr<SourceArtwork> AcceptedSource(const RgbaImage& pixels) {
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(pixels));
  return SourceArtwork{
      .id = "source-1",
      .name = "Boulder source",
      .source_path = "source_art/props/source-1.png",
      .provenance =
          ImportedArtworkProvenance{
              .original_filename = "boulder.png",
              .imported_at_utc = "2026-08-16T15:04:05Z",
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

absl::StatusOr<PreparedPropAsset> PrepareFor(const PropArtworkEditorModel& model) {
  PreparePropAssetRequest request{
      .name = model.name(),
      .terrain_recipe_id = model.settings().terrain_recipe_id,
      .style = model.settings().style,
      .pipeline = model.settings().pipeline,
      .ids =
          PropAssetIds{
              .texture_id = "texture-1",
              .sprite_id = "sprite-1",
              .blueprint_id = "blueprint-1",
              .recipe_id = "recipe-1",
          },
  };
  return PreparePropAsset(*model.source(), *model.source_pixels(), request);
}

class PropArtworkEditorModelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    pixels_ = SourcePixels();
    ASSERT_OK_AND_ASSIGN(source_, AcceptedSource(pixels_));
    ASSERT_OK(model_.SelectSource(source_, pixels_));
    ASSERT_OK(model_.AttachTerrain(terrain_));
    model_.settings().pipeline.isolation.minimum_subject_area = 16;
    model_.settings().pipeline.composition.canvas_tiles_wide = 2;
    model_.settings().pipeline.composition.canvas_tiles_high = 2;
    model_.MarkInputsChanged();
  }

  RgbaImage pixels_;
  SourceArtwork source_;
  TerrainRecipe terrain_ = Terrain();
  PropArtworkEditorModel model_;
};

TEST_F(PropArtworkEditorModelTest, AttachingTerrainResolvesTheFullPalette) {
  ASSERT_TRUE(model_.terrain_recipe().has_value());
  EXPECT_EQ(model_.settings().terrain_recipe_id, terrain_.id);
  EXPECT_EQ(model_.settings().style.tile_size, terrain_.config.tile_size);
  ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette expected,
                       ResolveTerrainPalette(terrain_.config));
  EXPECT_EQ(model_.settings().style.palette.colors, expected.colors);
}

TEST_F(PropArtworkEditorModelTest, SupersededWorkerResultIsDiscarded) {
  const uint64_t prepared_revision = model_.revision();
  ASSERT_OK_AND_ASSIGN(PreparedPropAsset prepared, PrepareFor(model_));
  model_.settings().pipeline.composition.canvas_tiles_wide = 3;
  model_.MarkInputsChanged();

  const absl::Status status =
      model_.AcceptPrepared(prepared_revision, std::move(prepared), std::nullopt);
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_FALSE(model_.HasPreparedResult());
}

TEST_F(PropArtworkEditorModelTest, PreviewPolicyDoesNotChangeFinishedPixels) {
  ASSERT_OK_AND_ASSIGN(PreparedPropAsset finished_prepared, PrepareFor(model_));
  ASSERT_OK(model_.AcceptPrepared(model_.revision(), std::move(finished_prepared), std::nullopt));

  ASSERT_NE(model_.PreviewImage(), nullptr);
  const std::vector<uint8_t> final_pixels = model_.PreviewImage()->pixels;

  model_.SetPreviewPolicy(PropPreviewPolicy::kReviewEachStage);
  EXPECT_FALSE(model_.HasPreparedResult());
  ASSERT_OK_AND_ASSIGN(PreparedPropAsset review_prepared, PrepareFor(model_));
  ASSERT_OK(model_.AcceptPrepared(model_.revision(), std::move(review_prepared), std::nullopt));
  model_.SetPreviewStage(PropPreviewStage::kCleanup);
  ASSERT_NE(model_.PreviewImage(), nullptr);
  EXPECT_EQ(model_.PreviewImage()->pixels, final_pixels);
}

TEST_F(PropArtworkEditorModelTest, ReviewNavigationVisitsEveryRetainedStage) {
  model_.SetPreviewPolicy(PropPreviewPolicy::kReviewEachStage);
  ASSERT_OK_AND_ASSIGN(PreparedPropAsset prepared, PrepareFor(model_));
  ASSERT_OK(model_.AcceptPrepared(model_.revision(), std::move(prepared), std::nullopt));
  model_.SetPreviewStage(PropPreviewStage::kSource);

  for (int stage = static_cast<int>(PropPreviewStage::kSource);
       stage <= static_cast<int>(PropPreviewStage::kCleanup); ++stage) {
    EXPECT_EQ(model_.preview_stage(), static_cast<PropPreviewStage>(stage));
    EXPECT_NE(model_.PreviewImage(), nullptr);
    model_.NextPreviewStage();
  }
}

TEST_F(PropArtworkEditorModelTest, PreviewAnchorTracksTheDisplayedArtifact) {
  model_.SetPreviewPolicy(PropPreviewPolicy::kReviewEachStage);
  ASSERT_OK_AND_ASSIGN(PreparedPropAsset prepared, PrepareFor(model_));
  const PropPreviewAnchor cleanup_anchor{
      .x = prepared.artwork.finished.anchor_x,
      .y = prepared.artwork.finished.anchor_y,
  };
  PropArtworkContextPreview context{
      .image = RgbaImage{.width = 16, .height = 16},
      .anchor_x = 9,
      .anchor_y = 11,
  };
  context.image.pixels.assign(16 * 16 * 4, 255);
  ASSERT_OK(model_.AcceptPrepared(model_.revision(), std::move(prepared), std::move(context)));

  model_.SetPreviewStage(PropPreviewStage::kIsolation);
  EXPECT_FALSE(model_.PreviewAnchor().has_value());

  model_.SetPreviewStage(PropPreviewStage::kCleanup);
  ASSERT_TRUE(model_.PreviewAnchor().has_value());
  EXPECT_EQ(model_.PreviewAnchor()->x, cleanup_anchor.x);
  EXPECT_EQ(model_.PreviewAnchor()->y, cleanup_anchor.y);

  model_.SetPreviewStage(PropPreviewStage::kContext);
  ASSERT_TRUE(model_.PreviewAnchor().has_value());
  EXPECT_EQ(model_.PreviewAnchor()->x, 9);
  EXPECT_EQ(model_.PreviewAnchor()->y, 11);
}

TEST_F(PropArtworkEditorModelTest, SaveAsKeepsSourceAndSettingsButDropsOutputIdentity) {
  ASSERT_OK_AND_ASSIGN(PreparedPropAsset prepared, PrepareFor(model_));
  const PropRecipe committed = prepared.recipe;
  ASSERT_OK(model_.AcceptPrepared(model_.revision(), std::move(prepared), std::nullopt));
  model_.BindCommittedRecipe(committed);

  model_.StartRecipeCopy();

  EXPECT_FALSE(model_.active_recipe().has_value());
  ASSERT_TRUE(model_.source().has_value());
  EXPECT_EQ(model_.source()->id, source_.id);
  EXPECT_EQ(model_.settings().terrain_recipe_id, terrain_.id);
  EXPECT_FALSE(model_.HasPreparedResult());
}

TEST_F(PropArtworkEditorModelTest, ExistingRecipeKeepsItsSourceUntilSaveAs) {
  ASSERT_OK_AND_ASSIGN(PreparedPropAsset prepared, PrepareFor(model_));
  ASSERT_OK(model_.LoadRecipe(prepared.recipe, source_, pixels_, terrain_));

  SourceArtwork another = source_;
  another.id = "source-2";
  another.source_path = "source_art/props/source-2.png";
  EXPECT_EQ(model_.SelectSource(std::move(another), pixels_).code(),
            absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
