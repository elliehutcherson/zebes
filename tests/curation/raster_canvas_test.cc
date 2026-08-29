#include "curation/raster_canvas.h"

#include <cstddef>
#include <cstdint>

#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

RgbaColor8 Pixel(const RgbaImage& image, int x, int y) {
  const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
  return {
      .red = image.pixels[offset],
      .green = image.pixels[offset + 1],
      .blue = image.pixels[offset + 2],
      .alpha = image.pixels[offset + 3],
  };
}

TEST(CurationRasterTest, NearestCompositionScalesAndClipsWithoutAPlatformRenderer) {
  ASSERT_OK_AND_ASSIGN(RgbaImage destination,
                       CreateSolidRgbaImage(4, 2, {.red = 0, .green = 0, .blue = 0}));
  ASSERT_OK_AND_ASSIGN(RgbaImage source,
                       CreateSolidRgbaImage(2, 1, {.red = 255, .green = 0, .blue = 0}));
  source.pixels[4] = 0;
  source.pixels[6] = 255;

  ASSERT_OK(CompositeRgbaNearest(destination, source, {.x = 0, .y = 0, .width = 2, .height = 1},
                                 {.x = -1, .y = 0, .width = 6, .height = 2}));

  EXPECT_EQ(Pixel(destination, 0, 0).red, 255);
  EXPECT_EQ(Pixel(destination, 1, 1).red, 255);
  EXPECT_EQ(Pixel(destination, 2, 0).blue, 255);
  EXPECT_EQ(Pixel(destination, 3, 1).blue, 255);
}

TEST(CurationRasterTest, SourceOverAlphaBlendsAgainstTheReviewBackground) {
  ASSERT_OK_AND_ASSIGN(
      RgbaImage destination,
      CreateSolidRgbaImage(1, 1, {.red = 0, .green = 0, .blue = 255, .alpha = 255}));
  ASSERT_OK_AND_ASSIGN(
      RgbaImage source,
      CreateSolidRgbaImage(1, 1, {.red = 255, .green = 0, .blue = 0, .alpha = 128}));

  ASSERT_OK(CompositeRgbaNearest(destination, source, {.x = 0, .y = 0, .width = 1, .height = 1},
                                 {.x = 0, .y = 0, .width = 1, .height = 1}));

  EXPECT_NEAR(Pixel(destination, 0, 0).red, 128, 1);
  EXPECT_NEAR(Pixel(destination, 0, 0).blue, 127, 1);
  EXPECT_EQ(Pixel(destination, 0, 0).alpha, 255);
}

TEST(CurationRasterTest, RejectsInvalidSourceGeometry) {
  ASSERT_OK_AND_ASSIGN(RgbaImage image, CreateSolidRgbaImage(2, 2, {}));
  EXPECT_FALSE(CompositeRgbaNearest(image, image, {.x = 1, .y = 1, .width = 2, .height = 2},
                                    {.x = 0, .y = 0, .width = 2, .height = 2})
                   .ok());
}

TEST(CurationRasterTest, DrawsSharedClippedAnnotationsAndRejectsReversedBounds) {
  ASSERT_OK_AND_ASSIGN(RgbaImage image, CreateSolidRgbaImage(5, 5, {}));
  const RgbaColor8 yellow{.red = 255, .green = 220, .blue = 40};

  ASSERT_OK(DrawRgbaCross(image, 0, 0, 2, yellow));
  ASSERT_OK(DrawRgbaOutline(image, 2, 2, 4, 4, yellow));

  EXPECT_EQ(Pixel(image, 0, 0).red, 255);
  EXPECT_EQ(Pixel(image, 2, 0).green, 220);
  EXPECT_EQ(Pixel(image, 3, 3).red, 0);
  EXPECT_EQ(Pixel(image, 4, 4).blue, 40);
  EXPECT_TRUE(absl::IsInvalidArgument(DrawRgbaOutline(image, 3, 2, 2, 4, yellow)));
}

}  // namespace
}  // namespace zebes
