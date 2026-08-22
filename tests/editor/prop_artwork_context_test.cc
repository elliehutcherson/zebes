#include "editor/prop_artwork_editor/prop_artwork_context.h"

#include <cstddef>

#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

TEST(PropArtworkContextTest, CompositesAPropBesideRealTerrainWithoutChangingTheProp) {
  PropArtwork prop;
  prop.image = RgbaImage{.width = 8, .height = 8};
  prop.image.pixels.assign(8 * 8 * 4, 0);
  for (int y = 2; y < 8; ++y) {
    for (int x = 1; x < 7; ++x) {
      const size_t offset = (static_cast<size_t>(y) * 8 + x) * 4;
      prop.image.pixels[offset + 0] = 200;
      prop.image.pixels[offset + 1] = 100;
      prop.image.pixels[offset + 2] = 50;
      prop.image.pixels[offset + 3] = 255;
    }
  }
  prop.anchor_x = 4;
  prop.anchor_y = 7;
  const RgbaImage original = prop.image;
  TerrainGenConfig terrain;
  terrain.tile_size = 8;
  terrain.supersample = 1;

  ASSERT_OK_AND_ASSIGN(
      const PropArtworkContextPreview preview,
      BuildPropArtworkContextPreview(prop, terrain, PropAttachmentMode::kGrounded));

  EXPECT_EQ(preview.image.width, 96);
  EXPECT_EQ(preview.image.height, 72);
  EXPECT_TRUE(preview.image.IsValid());
  EXPECT_GT(preview.anchor_x, 0);
  EXPECT_GT(preview.anchor_y, 0);
  EXPECT_EQ(prop.image.pixels, original.pixels);
}

TEST(PropArtworkContextTest, ExpandsToKeepTheCompletePropTextureInFrame) {
  PropArtwork prop;
  prop.image = RgbaImage{.width = 8, .height = 80};
  prop.image.pixels.resize(8 * 80 * 4);
  for (size_t pixel = 0; pixel < 8 * 80; ++pixel) {
    prop.image.pixels[pixel * 4 + 0] = 17;
    prop.image.pixels[pixel * 4 + 1] = 231;
    prop.image.pixels[pixel * 4 + 2] = 93;
    prop.image.pixels[pixel * 4 + 3] = 255;
  }
  prop.anchor_x = 4;
  prop.anchor_y = 79;
  TerrainGenConfig terrain;
  terrain.tile_size = 8;
  terrain.supersample = 1;

  ASSERT_OK_AND_ASSIGN(
      const PropArtworkContextPreview preview,
      BuildPropArtworkContextPreview(prop, terrain, PropAttachmentMode::kGrounded));

  const int prop_top = preview.anchor_y - prop.anchor_y;
  const int prop_left = preview.anchor_x - prop.anchor_x;
  EXPECT_GE(prop_top, terrain.tile_size);
  EXPECT_GE(prop_left, terrain.tile_size);
  EXPECT_LE(prop_top + prop.image.height, preview.image.height - terrain.tile_size);
  EXPECT_LE(prop_left + prop.image.width, preview.image.width - terrain.tile_size);

  size_t retained_prop_pixels = 0;
  for (size_t pixel = 0; pixel < static_cast<size_t>(preview.image.width) * preview.image.height;
       ++pixel) {
    const size_t offset = pixel * 4;
    if (preview.image.pixels[offset + 0] == 17 && preview.image.pixels[offset + 1] == 231 &&
        preview.image.pixels[offset + 2] == 93 && preview.image.pixels[offset + 3] == 255) {
      ++retained_prop_pixels;
    }
  }
  EXPECT_EQ(retained_prop_pixels, 8 * 80);
}

TEST(PropArtworkContextTest, PlacesEachAttachmentModeInItsMatchingScene) {
  PropArtwork prop;
  prop.image = RgbaImage{.width = 1, .height = 1, .pixels = {0, 0, 0, 0}};
  TerrainGenConfig terrain;
  terrain.tile_size = 8;
  terrain.supersample = 1;

  ASSERT_OK_AND_ASSIGN(
      const PropArtworkContextPreview grounded,
      BuildPropArtworkContextPreview(prop, terrain, PropAttachmentMode::kGrounded));
  ASSERT_OK_AND_ASSIGN(const PropArtworkContextPreview ceiling,
                       BuildPropArtworkContextPreview(prop, terrain, PropAttachmentMode::kCeiling));
  ASSERT_OK_AND_ASSIGN(const PropArtworkContextPreview free,
                       BuildPropArtworkContextPreview(prop, terrain, PropAttachmentMode::kFree));

  const auto is_checker = [](const PropArtworkContextPreview& preview, int x, int y) {
    const size_t offset = (static_cast<size_t>(y) * preview.image.width + x) * 4;
    const uint8_t red = preview.image.pixels[offset + 0];
    return (red == 42 || red == 52) && preview.image.pixels[offset + 1] == red &&
           preview.image.pixels[offset + 2] == red;
  };
  EXPECT_FALSE(is_checker(grounded, grounded.anchor_x, grounded.anchor_y));
  EXPECT_TRUE(is_checker(grounded, grounded.anchor_x, grounded.anchor_y - 1));
  EXPECT_FALSE(is_checker(ceiling, ceiling.anchor_x, ceiling.anchor_y));
  EXPECT_TRUE(is_checker(ceiling, ceiling.anchor_x, ceiling.anchor_y + 1));
  EXPECT_EQ(free.anchor_x, free.image.width / 2);
  EXPECT_EQ(free.anchor_y, free.image.height / 2);
}

TEST(PropArtworkContextTest, FreePropFollowsTheRequestedPreviewPositionWithoutAccumulatingCopies) {
  PropArtwork prop{
      .image = RgbaImage{.width = 2,
                         .height = 2,
                         .pixels = {231, 17, 93, 255, 231, 17, 93, 255, 231, 17, 93, 255, 231, 17,
                                    93, 255}},
      .anchor_x = 1,
      .anchor_y = 1,
  };
  TerrainGenConfig terrain;
  terrain.tile_size = 8;
  terrain.supersample = 1;
  ASSERT_OK_AND_ASSIGN(PropArtworkContextPreview preview,
                       BuildPropArtworkContextPreview(prop, terrain, PropAttachmentMode::kFree));

  ASSERT_OK(MovePropArtworkContextPreview(prop, 20, 24, &preview));

  EXPECT_EQ(preview.anchor_x, 20);
  EXPECT_EQ(preview.anchor_y, 24);
  EXPECT_EQ(preview.prop_left, 19);
  EXPECT_EQ(preview.prop_top, 23);
  size_t prop_pixels = 0;
  for (size_t pixel = 0; pixel < static_cast<size_t>(preview.image.width) * preview.image.height;
       ++pixel) {
    const size_t offset = pixel * 4;
    if (preview.image.pixels[offset + 0] == 231 && preview.image.pixels[offset + 1] == 17 &&
        preview.image.pixels[offset + 2] == 93) {
      ++prop_pixels;
    }
  }
  EXPECT_EQ(prop_pixels, 4);
}

TEST(PropArtworkContextTest, GroundedPropFollowsTerrainAndStaysCompletelyInFrame) {
  PropArtwork prop;
  prop.image = RgbaImage{.width = 8, .height = 8};
  prop.image.pixels.assign(8 * 8 * 4, 255);
  prop.anchor_x = 4;
  prop.anchor_y = 7;
  TerrainGenConfig terrain;
  terrain.tile_size = 8;
  terrain.supersample = 1;
  ASSERT_OK_AND_ASSIGN(
      PropArtworkContextPreview preview,
      BuildPropArtworkContextPreview(prop, terrain, PropAttachmentMode::kGrounded));
  const int original_x = preview.anchor_x;

  ASSERT_OK(MovePropArtworkContextPreview(prop, preview.terrain_left, 0, &preview));

  EXPECT_NE(preview.anchor_x, original_x);
  EXPECT_GE(preview.prop_left, 0);
  EXPECT_GE(preview.prop_top, 0);
  EXPECT_LE(preview.prop_left + prop.image.width, preview.image.width);
  EXPECT_LE(preview.prop_top + prop.image.height, preview.image.height);
  const int terrain_x = preview.anchor_x - preview.terrain_left;
  const int terrain_y = preview.anchor_y - preview.terrain_top;
  ASSERT_GE(terrain_x, 0);
  ASSERT_LT(terrain_x, preview.terrain.width);
  ASSERT_GE(terrain_y, 0);
  ASSERT_LT(terrain_y, preview.terrain.height);
  const size_t surface = (static_cast<size_t>(terrain_y) * preview.terrain.width + terrain_x) * 4;
  EXPECT_NE(preview.terrain.pixels[surface + 3], 0);
  if (terrain_y > 0) {
    const size_t above =
        (static_cast<size_t>(terrain_y - 1) * preview.terrain.width + terrain_x) * 4;
    EXPECT_EQ(preview.terrain.pixels[above + 3], 0);
  }
}

}  // namespace
}  // namespace zebes
