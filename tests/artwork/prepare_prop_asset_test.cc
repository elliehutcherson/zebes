#include "artwork/prepare_prop_asset.h"

#include <cstddef>

#include "common/image_digest.h"
#include "common/status_macros.h"
#include "gtest/gtest.h"
#include "terrain/terrain_palette.h"
#include "terrain/terrain_style.h"
#include "tests/macros.h"

namespace zebes {
namespace {

RgbaImage TestSource() {
  RgbaImage image{.width = 32, .height = 24};
  image.pixels.assign(static_cast<size_t>(image.width) * image.height * 4, 0);
  for (size_t pixel = 0; pixel < static_cast<size_t>(image.width) * image.height; ++pixel) {
    image.pixels[pixel * 4 + 0] = 236;
    image.pixels[pixel * 4 + 1] = 232;
    image.pixels[pixel * 4 + 2] = 228;
    image.pixels[pixel * 4 + 3] = 255;
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

absl::StatusOr<SourceArtwork> TestSourceDefinition(const RgbaImage& pixels) {
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(pixels));
  return SourceArtwork{
      .id = "source-1",
      .name = "Boulder source",
      .source_path = "source_art/source-1.png",
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

absl::StatusOr<PreparePropAssetRequest> TestRequest() {
  const TerrainGenConfig terrain;
  ASSIGN_OR_RETURN(const ResolvedTerrainPalette palette, ResolveTerrainPalette(terrain));
  PreparePropAssetRequest request{
      .name = "Cave boulder",
      .terrain_recipe_id = "terrain-1",
      .style = PropArtworkStyle{.tile_size = 8, .pixel_block_size = 1, .palette = palette},
      .ids =
          PropAssetIds{
              .texture_id = "texture-1",
              .sprite_id = "sprite-1",
              .blueprint_id = "blueprint-1",
              .recipe_id = "recipe-1",
          },
  };
  request.pipeline.isolation.minimum_subject_area = 16;
  request.pipeline.composition = PropCompositionConfig{
      .canvas_tiles_wide = 2,
      .canvas_tiles_high = 1,
      .padding_fraction = 0.05f,
  };
  request.pipeline.cleanup.contact_tolerance = 2;
  return request;
}

TEST(PreparePropAssetTest, BuildsACompleteColliderFreeRuntimeBundle) {
  const RgbaImage pixels = TestSource();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, TestSourceDefinition(pixels));
  ASSERT_OK_AND_ASSIGN(const PreparePropAssetRequest request, TestRequest());

  ASSERT_OK_AND_ASSIGN(const PreparedPropAsset prepared, PreparePropAsset(source, pixels, request));

  EXPECT_EQ(prepared.texture.path, "textures/props/texture-1.png");
  ASSERT_EQ(prepared.sprite.frames.size(), 1u);
  const SpriteFrame& frame = prepared.sprite.frames.front();
  EXPECT_EQ(frame.texture_w, 16);
  EXPECT_EQ(frame.texture_h, 8);
  EXPECT_EQ(frame.offset_x, -prepared.artwork.finished.anchor_x);
  EXPECT_EQ(frame.offset_y, -prepared.artwork.finished.anchor_y);
  ASSERT_EQ(prepared.blueprint.states.size(), 1u);
  EXPECT_TRUE(prepared.blueprint.states.front().collider_id.empty());
  EXPECT_EQ(prepared.blueprint.states.front().sprite_id, prepared.sprite.id);
  EXPECT_EQ(prepared.blueprint.states.front().placement_mode, BlueprintPlacementMode::kGrounded);
  EXPECT_EQ(prepared.recipe.expected_frame, frame);
  EXPECT_EQ(prepared.recipe.final_pixel_digest.size(), 64u);
  EXPECT_OK(ValidatePreparedPropAsset(prepared));
}

TEST(PreparePropAssetTest, IsByteDeterministicForTheSameAcceptedSourceAndRequest) {
  const RgbaImage pixels = TestSource();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, TestSourceDefinition(pixels));
  ASSERT_OK_AND_ASSIGN(const PreparePropAssetRequest request, TestRequest());

  ASSERT_OK_AND_ASSIGN(const PreparedPropAsset first, PreparePropAsset(source, pixels, request));
  ASSERT_OK_AND_ASSIGN(const PreparedPropAsset second, PreparePropAsset(source, pixels, request));

  EXPECT_EQ(first.artwork.finished.image.pixels, second.artwork.finished.image.pixels);
  EXPECT_EQ(first.recipe.final_pixel_digest, second.recipe.final_pixel_digest);
  EXPECT_EQ(first.sprite, second.sprite);
  EXPECT_EQ(first.blueprint, second.blueprint);
}

TEST(PreparePropAssetTest, FreeAnchorBecomesTheExactSpriteOffset) {
  const RgbaImage pixels = TestSource();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, TestSourceDefinition(pixels));
  ASSERT_OK_AND_ASSIGN(PreparePropAssetRequest request, TestRequest());
  request.pipeline.composition.attachment = PropAttachmentConfig{
      .mode = PropAttachmentMode::kFree,
      .free_anchor = PropFreeAnchor{.x = 2, .y = 6},
  };

  ASSERT_OK_AND_ASSIGN(const PreparedPropAsset prepared, PreparePropAsset(source, pixels, request));
  EXPECT_EQ(prepared.artwork.finished.anchor_x, 2);
  EXPECT_EQ(prepared.artwork.finished.anchor_y, 6);
  EXPECT_EQ(prepared.sprite.frames.front().offset_x, -2);
  EXPECT_EQ(prepared.sprite.frames.front().offset_y, -6);
  EXPECT_EQ(prepared.blueprint.states.front().placement_mode, BlueprintPlacementMode::kFree);
}

TEST(PreparePropAssetTest, CeilingAttachmentInitializesCeilingBlueprintPlacement) {
  const RgbaImage pixels = TestSource();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, TestSourceDefinition(pixels));
  ASSERT_OK_AND_ASSIGN(PreparePropAssetRequest request, TestRequest());
  request.pipeline.composition.attachment.mode = PropAttachmentMode::kCeiling;

  ASSERT_OK_AND_ASSIGN(const PreparedPropAsset prepared, PreparePropAsset(source, pixels, request));

  EXPECT_EQ(prepared.blueprint.states.front().placement_mode, BlueprintPlacementMode::kCeiling);
}

TEST(PreparePropAssetTest, RefusesPixelsThatDoNotMatchTheAcceptedSource) {
  RgbaImage pixels = TestSource();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, TestSourceDefinition(pixels));
  ASSERT_OK_AND_ASSIGN(const PreparePropAssetRequest request, TestRequest());
  pixels.pixels[0] ^= 0xff;

  const absl::Status status = PreparePropAsset(source, pixels, request).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST(PreparePropAssetTest, RefusesUnsafeNamesAndIdsBeforeRunningThePipeline) {
  const RgbaImage pixels = TestSource();
  ASSERT_OK_AND_ASSIGN(const SourceArtwork source, TestSourceDefinition(pixels));
  ASSERT_OK_AND_ASSIGN(PreparePropAssetRequest request, TestRequest());
  request.name = "../boulder";

  EXPECT_EQ(PreparePropAsset(source, pixels, request).status().code(),
            absl::StatusCode::kInvalidArgument);

  request.name = "Boulder";
  request.ids.texture_id = "texture/1";
  EXPECT_EQ(PreparePropAsset(source, pixels, request).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
