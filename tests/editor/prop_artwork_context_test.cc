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

  ASSERT_OK_AND_ASSIGN(const PropArtworkContextPreview preview,
                       BuildPropArtworkContextPreview(prop, terrain));

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

  ASSERT_OK_AND_ASSIGN(const PropArtworkContextPreview preview,
                       BuildPropArtworkContextPreview(prop, terrain));

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

}  // namespace
}  // namespace zebes
