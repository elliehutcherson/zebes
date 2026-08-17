#include "artwork/regenerate_prop_asset.h"

#include <cstddef>

#include "artwork/prepare_prop_asset.h"
#include "common/image_digest.h"
#include "gtest/gtest.h"
#include "terrain/terrain_palette.h"
#include "terrain/terrain_style.h"
#include "tests/macros.h"

namespace zebes {
namespace {

RgbaImage SourcePixels() {
  RgbaImage image{.width = 32, .height = 24};
  image.pixels.assign(static_cast<size_t>(image.width) * image.height * 4, 255);
  for (size_t pixel = 0; pixel < static_cast<size_t>(image.width) * image.height; ++pixel) {
    image.pixels[pixel * 4 + 0] = 236;
    image.pixels[pixel * 4 + 1] = 232;
    image.pixels[pixel * 4 + 2] = 228;
  }
  for (int y = 6; y < 19; ++y) {
    for (int x = 7; x < 25; ++x) {
      const size_t pixel = (static_cast<size_t>(y) * image.width + x) * 4;
      image.pixels[pixel + 0] = 74;
      image.pixels[pixel + 1] = 68;
      image.pixels[pixel + 2] = 64;
    }
  }
  return image;
}

class RegeneratePropAssetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    source_pixels_ = SourcePixels();
    ASSERT_OK_AND_ASSIGN(const std::string source_digest, RgbaImageDigest(source_pixels_));
    source_ = SourceArtwork{
        .id = "source-1",
        .name = "Boulder source",
        .source_path = "source_art/props/source-1.png",
        .provenance =
            ImportedArtworkProvenance{
                .original_filename = "boulder.png",
                .imported_at_utc = "2026-08-16T15:04:05Z",
            },
        .width = source_pixels_.width,
        .height = source_pixels_.height,
        .content_digest = source_digest,
    };
    const TerrainGenConfig terrain;
    ASSERT_OK_AND_ASSIGN(const ResolvedTerrainPalette palette, ResolveTerrainPalette(terrain));
    settings_.style = PropArtworkStyle{.tile_size = 8, .pixel_block_size = 1, .palette = palette};
    settings_.pipeline.isolation.minimum_subject_area = 16;
    settings_.pipeline.composition = PropCompositionConfig{
        .canvas_tiles_wide = 2,
        .canvas_tiles_high = 1,
        .padding_fraction = 0.05f,
    };
    settings_.pipeline.cleanup.grounded_tolerance = 2;
    const PreparePropAssetRequest request{
        .name = "Cave boulder",
        .style = settings_.style,
        .pipeline = settings_.pipeline,
        .ids =
            PropAssetIds{
                .texture_id = "texture-1",
                .sprite_id = "sprite-1",
                .blueprint_id = "blueprint-1",
                .recipe_id = "recipe-1",
            },
    };
    ASSERT_OK_AND_ASSIGN(PreparedPropAsset created,
                         PreparePropAsset(source_, source_pixels_, request));
    texture_ = std::move(created.texture);
    texture_pixels_ = created.artwork.finished.image;
    sprite_ = std::move(created.sprite);
    recipe_ = std::move(created.recipe);
  }

  RgbaImage source_pixels_;
  SourceArtwork source_;
  Texture texture_;
  RgbaImage texture_pixels_;
  Sprite sprite_;
  PropRecipe recipe_;
  PropRegenerationSettings settings_;
};

TEST_F(RegeneratePropAssetTest, PreservesIdsWhileAllowingRecipeOwnedGeometryToChange) {
  settings_.pipeline.composition.canvas_tiles_wide = 3;
  settings_.pipeline.composition.canvas_tiles_high = 2;

  ASSERT_OK_AND_ASSIGN(const PreparedPropRegeneration prepared,
                       PreparePropRegeneration(source_, source_pixels_, recipe_, texture_,
                                               texture_pixels_, sprite_, settings_));

  EXPECT_EQ(prepared.updated_recipe.id, recipe_.id);
  EXPECT_EQ(prepared.updated_recipe.texture_id, recipe_.texture_id);
  EXPECT_EQ(prepared.updated_recipe.sprite_id, recipe_.sprite_id);
  EXPECT_EQ(prepared.updated_recipe.blueprint_id, recipe_.blueprint_id);
  ASSERT_EQ(prepared.updated_sprite.frames.size(), 1u);
  EXPECT_EQ(prepared.updated_sprite.frames.front().texture_w, 24);
  EXPECT_EQ(prepared.updated_sprite.frames.front().texture_h, 16);
  EXPECT_NE(prepared.updated_recipe.expected_frame, recipe_.expected_frame);
  EXPECT_OK(ValidatePreparedPropRegeneration(prepared));
}

TEST_F(RegeneratePropAssetTest, RefusesTexturePixelsChangedOutsideTheRecipe) {
  texture_pixels_.pixels[0] ^= 0xff;

  const absl::Status status = PreparePropRegeneration(source_, source_pixels_, recipe_, texture_,
                                                      texture_pixels_, sprite_, settings_)
                                  .status();
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(RegeneratePropAssetTest, RefusesSpriteGeometryChangedOutsideTheRecipe) {
  ++sprite_.frames.front().offset_x;

  const absl::Status status = PreparePropRegeneration(source_, source_pixels_, recipe_, texture_,
                                                      texture_pixels_, sprite_, settings_)
                                  .status();
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(RegeneratePropAssetTest, RetainsEveryPreviewForEditorReview) {
  ASSERT_OK_AND_ASSIGN(const PreparedPropRegeneration prepared,
                       PreparePropRegeneration(source_, source_pixels_, recipe_, texture_,
                                               texture_pixels_, sprite_, settings_));

  EXPECT_TRUE(prepared.artwork.isolated.IsValid());
  EXPECT_TRUE(prepared.artwork.composed.IsValid());
  EXPECT_TRUE(prepared.artwork.rasterized.IsValid());
  EXPECT_TRUE(prepared.artwork.quantized.IsValid());
  EXPECT_TRUE(prepared.artwork.edge_treated.IsValid());
  EXPECT_TRUE(prepared.artwork.finished.IsValid());
}

}  // namespace
}  // namespace zebes
