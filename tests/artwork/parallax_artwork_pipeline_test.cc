#include "artwork/parallax_artwork_pipeline.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

RgbaImage SolidImage(int width, int height, RgbaColor color) {
  RgbaImage image{
      .width = width,
      .height = height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4),
  };
  for (size_t offset = 0; offset < image.pixels.size(); offset += 4) {
    image.pixels[offset + 0] = color.r;
    image.pixels[offset + 1] = color.g;
    image.pixels[offset + 2] = color.b;
    image.pixels[offset + 3] = color.a;
  }
  return image;
}

void PaintPixel(RgbaImage& image, int x, int y, RgbaColor color) {
  const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
  image.pixels[offset + 0] = color.r;
  image.pixels[offset + 1] = color.g;
  image.pixels[offset + 2] = color.b;
  image.pixels[offset + 3] = color.a;
}

ParallaxArtworkStyle PreserveStyle() {
  return {.pixel_block_size = 1, .quantize_to_palette = false};
}

ParallaxArtworkPipelineConfig SmallConfig() {
  return {
      .target_width = 4,
      .target_height = 4,
  };
}

TEST(ParallaxArtworkPipelineTest, OpaquePlateCropsToFillAndForcesEveryPixelOpaque) {
  RgbaImage source = SolidImage(8, 4, RgbaColor{10, 20, 30, 100});
  PaintPixel(source, 0, 0, RgbaColor{255, 0, 0, 0});

  ASSERT_OK_AND_ASSIGN(const ParallaxArtworkPipelineResult result,
                       RunParallaxArtworkPipeline(source, PreserveStyle(), SmallConfig()));

  EXPECT_EQ(result.finished.width, 4);
  EXPECT_EQ(result.finished.height, 4);
  for (size_t offset = 3; offset < result.finished.pixels.size(); offset += 4) {
    EXPECT_EQ(result.finished.pixels[offset], 255);
  }
}

TEST(ParallaxArtworkPipelineTest, TransparentOverlayFitsInsideAndKeepsPaddingTransparent) {
  const RgbaImage source = SolidImage(4, 2, RgbaColor{10, 20, 30, 128});
  ParallaxArtworkPipelineConfig config = SmallConfig();
  config.frame_policy = ParallaxArtworkFramePolicy::kFitInside;
  config.alpha_role = ParallaxArtworkAlphaRole::kTransparentOverlay;

  ASSERT_OK_AND_ASSIGN(const ParallaxArtworkPipelineResult result,
                       RunParallaxArtworkPipeline(source, PreserveStyle(), config));

  EXPECT_EQ(result.finished.pixels[3], 0);
  EXPECT_EQ(result.finished.pixels[(static_cast<size_t>(1) * 4) * 4 + 3], 128);
  EXPECT_EQ(result.finished.pixels[(static_cast<size_t>(3) * 4) * 4 + 3], 0);
}

TEST(ParallaxArtworkPipelineTest, MatteExtractionRemovesExteriorAndEnclosedMatte) {
  const RgbaColor matte{255, 0, 255, 255};
  const RgbaColor dark{10, 16, 59, 255};
  RgbaImage source = SolidImage(5, 5, matte);
  for (int y = 1; y < 4; ++y) {
    for (int x = 1; x < 4; ++x) PaintPixel(source, x, y, dark);
  }
  PaintPixel(source, 2, 2, matte);
  ParallaxArtworkStyle style{
      .pixel_block_size = 1,
      .quantize_to_palette = false,
      .palette = {dark},
  };
  ParallaxArtworkPipelineConfig config{
      .target_width = 5,
      .target_height = 5,
      .alpha_role = ParallaxArtworkAlphaRole::kTransparentOverlay,
      .overlay_extraction = ParallaxArtworkOverlayExtraction::kRemoveSolidMatte,
  };

  ASSERT_OK_AND_ASSIGN(const ParallaxArtworkPipelineResult result,
                       RunParallaxArtworkPipeline(source, style, config));

  EXPECT_EQ(result.matte_extracted.pixels[3], 0);
  const size_t center = (static_cast<size_t>(2) * 5 + 2) * 4;
  EXPECT_EQ(result.matte_extracted.pixels[center + 3], 0);
}

TEST(ParallaxArtworkPipelineTest, QuantizationUsesOnlyTheResolvedStylePalette) {
  const RgbaImage source = SolidImage(4, 4, RgbaColor{15, 20, 25, 255});
  ParallaxArtworkStyle style{
      .palette = {RgbaColor{10, 10, 10, 255}, RgbaColor{200, 200, 200, 255}}};

  ASSERT_OK_AND_ASSIGN(const ParallaxArtworkPipelineResult result,
                       RunParallaxArtworkPipeline(source, style, SmallConfig()));

  for (size_t offset = 0; offset < result.finished.pixels.size(); offset += 4) {
    EXPECT_EQ(result.finished.pixels[offset + 0], 10);
    EXPECT_EQ(result.finished.pixels[offset + 1], 10);
    EXPECT_EQ(result.finished.pixels[offset + 2], 10);
  }
}

TEST(ParallaxArtworkPipelineTest, RequestedRepeatReviewsAreWrappedAndReportEdgeFacts) {
  RgbaImage source = SolidImage(4, 4, RgbaColor{10, 10, 10, 255});
  PaintPixel(source, 3, 0, RgbaColor{20, 10, 10, 255});
  ParallaxArtworkPipelineConfig config = SmallConfig();
  config.review_repeat_x = true;
  config.review_repeat_y = true;

  ASSERT_OK_AND_ASSIGN(const ParallaxArtworkPipelineResult result,
                       RunParallaxArtworkPipeline(source, PreserveStyle(), config));

  ASSERT_TRUE(result.repeat_x_preview.has_value());
  EXPECT_EQ(result.repeat_x_preview->width, 12);
  EXPECT_EQ(result.repeat_x_preview->height, 4);
  ASSERT_TRUE(result.repeat_y_preview.has_value());
  EXPECT_EQ(result.repeat_y_preview->width, 4);
  EXPECT_EQ(result.repeat_y_preview->height, 12);
  EXPECT_EQ(result.repetition.horizontal.pixels_compared, 4);
  EXPECT_GT(result.repetition.horizontal.maximum_channel_difference, 0);
}

TEST(ParallaxArtworkPipelineTest, OpaquePlateRejectsFitAndOverlayOnlySettings) {
  ParallaxArtworkPipelineConfig config = SmallConfig();
  config.frame_policy = ParallaxArtworkFramePolicy::kFitInside;

  const absl::Status status = ValidateParallaxArtworkPipelineConfig(config, PreserveStyle());

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("opaque"), std::string_view::npos);
}

TEST(ParallaxArtworkPipelineTest, ConfigValidationNamesInvalidPolicyAndGeometry) {
  ParallaxArtworkPipelineConfig config = SmallConfig();
  config.frame_policy = static_cast<ParallaxArtworkFramePolicy>(99);
  absl::Status status = ValidateParallaxArtworkPipelineConfig(config, PreserveStyle());
  EXPECT_NE(status.message().find("frame policy"), std::string_view::npos);

  config = SmallConfig();
  config.target_width = 0;
  status = ValidateParallaxArtworkPipelineConfig(config, PreserveStyle());
  EXPECT_NE(status.message().find("target dimensions"), std::string_view::npos);
}

TEST(ParallaxArtworkPipelineTest, MatteExtractionRejectsAPlateWithNoMatchingMatte) {
  const RgbaColor dark{10, 16, 59, 255};
  const RgbaImage source = SolidImage(4, 4, dark);
  ParallaxArtworkStyle style{
      .pixel_block_size = 1,
      .quantize_to_palette = false,
      .palette = {dark},
  };
  ParallaxArtworkPipelineConfig config = SmallConfig();
  config.alpha_role = ParallaxArtworkAlphaRole::kTransparentOverlay;
  config.overlay_extraction = ParallaxArtworkOverlayExtraction::kRemoveSolidMatte;

  const absl::Status status = RunParallaxArtworkPipeline(source, style, config).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(status.message().find("no pixels matching"), std::string_view::npos);
}

TEST(ParallaxArtworkPipelineTest, ProcessingAndDigestsAreDeterministic) {
  const RgbaImage source = SolidImage(4, 4, RgbaColor{10, 20, 30, 255});

  ASSERT_OK_AND_ASSIGN(const ParallaxArtworkPipelineResult first,
                       RunParallaxArtworkPipeline(source, PreserveStyle(), SmallConfig()));
  ASSERT_OK_AND_ASSIGN(const ParallaxArtworkPipelineResult second,
                       RunParallaxArtworkPipeline(source, PreserveStyle(), SmallConfig()));

  EXPECT_EQ(first.source_digest, second.source_digest);
  EXPECT_EQ(first.final_digest, second.final_digest);
  EXPECT_EQ(first.finished.pixels, second.finished.pixels);
}

}  // namespace
}  // namespace zebes
