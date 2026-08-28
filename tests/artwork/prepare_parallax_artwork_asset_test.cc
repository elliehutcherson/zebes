#include "artwork/prepare_parallax_artwork_asset.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "artwork/redraw_parallax_artwork_asset.h"
#include "artwork/regenerate_parallax_artwork_asset.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

RgbaImage SourcePixels() {
  return {
      .width = 4,
      .height = 4,
      .pixels = std::vector<uint8_t>(4 * 4 * 4, 255),
  };
}

absl::StatusOr<SourceArtwork> RetainedSource(const RgbaImage& pixels) {
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(pixels));
  return SourceArtwork{
      .id = "source-id",
      .name = "Raw Cave Plate",
      .source_path = "source_art/source-id.png",
      .provenance =
          ImportedArtworkProvenance{
              .original_filename = "cave.png",
              .imported_at_utc = "2026-08-23T12:00:00Z",
          },
      .width = pixels.width,
      .height = pixels.height,
      .content_digest = digest,
  };
}

PrepareParallaxArtworkAssetRequest Request() {
  return {
      .name = "Far Cave Plate",
      .style = {.pixel_block_size = 1, .quantize_to_palette = false},
      .pipeline = {.target_width = 4, .target_height = 4},
      .ids = {.texture_id = "texture-id", .recipe_id = "recipe-id"},
  };
}

TEST(PrepareParallaxArtworkAssetTest, BuildsOneTextureAndItsReproducibleRecipe) {
  const RgbaImage pixels = SourcePixels();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, RetainedSource(pixels));

  ASSERT_OK_AND_ASSIGN(const PreparedParallaxArtworkAsset prepared,
                       PrepareParallaxArtworkAsset(source, pixels, Request()));

  EXPECT_EQ(prepared.texture.id, "texture-id");
  EXPECT_EQ(prepared.texture.path, "textures/parallax_artwork/texture-id.png");
  EXPECT_EQ(prepared.recipe.source_artwork_id, source.id);
  EXPECT_EQ(prepared.recipe.texture_id, prepared.texture.id);
  EXPECT_EQ(prepared.recipe.final_pixel_digest, prepared.artwork.final_digest);
  EXPECT_EQ(prepared.recipe.expected_width, 4);
  EXPECT_EQ(prepared.recipe.expected_height, 4);
  EXPECT_OK(ValidatePreparedParallaxArtworkAsset(prepared));
}

TEST(PrepareParallaxArtworkAssetTest, RejectsSourcePixelsThatDoNotMatchRetainedAuthority) {
  RgbaImage pixels = SourcePixels();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, RetainedSource(pixels));
  pixels.pixels[0] = 0;

  const absl::Status status = PrepareParallaxArtworkAsset(source, pixels, Request()).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST(PrepareParallaxArtworkAssetTest, RegenerationFromSameSourceIsPixelIdentical) {
  const RgbaImage pixels = SourcePixels();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, RetainedSource(pixels));
  ASSERT_OK_AND_ASSIGN(const PreparedParallaxArtworkAsset original,
                       PrepareParallaxArtworkAsset(source, pixels, Request()));
  const ParallaxArtworkRegenerationSettings settings{
      .terrain_recipe_id = original.recipe.terrain_recipe_id,
      .style = original.recipe.style,
      .pipeline = original.recipe.pipeline,
  };

  ASSERT_OK_AND_ASSIGN(
      const PreparedParallaxArtworkRegeneration regenerated,
      PrepareParallaxArtworkRegeneration(source, pixels, original.recipe, original.texture,
                                         original.artwork.finished, settings));

  EXPECT_EQ(regenerated.artwork.finished.pixels, original.artwork.finished.pixels);
  EXPECT_EQ(regenerated.updated_recipe.final_pixel_digest, original.recipe.final_pixel_digest);
  EXPECT_OK(ValidatePreparedParallaxArtworkRegeneration(regenerated));
}

TEST(PrepareParallaxArtworkAssetTest, RegenerationRefusesExternallyChangedTexturePixels) {
  const RgbaImage pixels = SourcePixels();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, RetainedSource(pixels));
  ASSERT_OK_AND_ASSIGN(const PreparedParallaxArtworkAsset original,
                       PrepareParallaxArtworkAsset(source, pixels, Request()));
  RgbaImage changed = original.artwork.finished;
  changed.pixels[0] = 0;

  const absl::Status status =
      PrepareParallaxArtworkRegeneration(source, pixels, original.recipe, original.texture, changed,
                                         {.terrain_recipe_id = original.recipe.terrain_recipe_id,
                                          .style = original.recipe.style,
                                          .pipeline = original.recipe.pipeline})
          .status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST(PrepareParallaxArtworkAssetTest, RedrawPreservesIdsAndAdvancesBothPixelDigests) {
  const RgbaImage pixels = SourcePixels();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, RetainedSource(pixels));
  ASSERT_OK_AND_ASSIGN(const PreparedParallaxArtworkAsset original,
                       PrepareParallaxArtworkAsset(source, pixels, Request()));
  RgbaImage replacement = pixels;
  replacement.pixels[0] = 17;

  ASSERT_OK_AND_ASSIGN(
      const PreparedParallaxArtworkRedraw redraw,
      PrepareParallaxArtworkRedraw(source, pixels,
                                   GeneratedArtworkProvenance{
                                       .provider = "imagegen",
                                       .model = "builtin",
                                       .submitted_prompt = "redraw the lateral edges",
                                       .generated_at_utc = "2026-08-27T22:00:00Z",
                                   },
                                   replacement, original.recipe, original.texture,
                                   original.artwork.finished));

  EXPECT_EQ(redraw.updated_source.id, source.id);
  EXPECT_EQ(redraw.updated_source.source_path, source.source_path);
  EXPECT_NE(redraw.updated_source.content_digest, source.content_digest);
  EXPECT_EQ(redraw.updated_recipe.id, original.recipe.id);
  EXPECT_EQ(redraw.updated_recipe.texture_id, original.recipe.texture_id);
  EXPECT_NE(redraw.updated_recipe.final_pixel_digest, original.recipe.final_pixel_digest);
  EXPECT_OK(ValidatePreparedParallaxArtworkRedraw(redraw));
}

TEST(PrepareParallaxArtworkAssetTest, RedrawValidationRefusesRecipeSettingChanges) {
  const RgbaImage pixels = SourcePixels();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, RetainedSource(pixels));
  ASSERT_OK_AND_ASSIGN(const PreparedParallaxArtworkAsset original,
                       PrepareParallaxArtworkAsset(source, pixels, Request()));
  RgbaImage replacement = pixels;
  replacement.pixels[0] = 17;
  ASSERT_OK_AND_ASSIGN(
      PreparedParallaxArtworkRedraw redraw,
      PrepareParallaxArtworkRedraw(source, pixels,
                                   GeneratedArtworkProvenance{
                                       .provider = "imagegen",
                                       .model = "builtin",
                                       .submitted_prompt = "redraw the lateral edges",
                                       .generated_at_utc = "2026-08-27T22:00:00Z",
                                   },
                                   replacement, original.recipe, original.texture,
                                   original.artwork.finished));
  redraw.updated_recipe.pipeline.review_repeat_x = true;

  EXPECT_EQ(ValidatePreparedParallaxArtworkRedraw(redraw).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(PrepareParallaxArtworkAssetTest, ValidationRejectsChangedOutputIdentity) {
  const RgbaImage pixels = SourcePixels();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, RetainedSource(pixels));
  ASSERT_OK_AND_ASSIGN(PreparedParallaxArtworkAsset prepared,
                       PrepareParallaxArtworkAsset(source, pixels, Request()));
  prepared.texture.path = "textures/unowned.png";

  EXPECT_EQ(ValidatePreparedParallaxArtworkAsset(prepared).code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
