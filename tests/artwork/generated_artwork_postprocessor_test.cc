#include "artwork/generated_artwork_postprocessor.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "terrain/terrain_palette.h"
#include "tests/macros.h"

namespace zebes {
namespace {

RgbaImage SolidImage(int width, int height, RgbaColor color) {
  RgbaImage image;
  image.width = width;
  image.height = height;
  image.pixels.resize(static_cast<size_t>(width) * height * 4);
  for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height; ++pixel) {
    image.pixels[pixel * 4 + 0] = color.r;
    image.pixels[pixel * 4 + 1] = color.g;
    image.pixels[pixel * 4 + 2] = color.b;
    image.pixels[pixel * 4 + 3] = color.a;
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

void PaintRect(RgbaImage& image, int left, int top, int width, int height, RgbaColor color) {
  for (int y = top; y < top + height; ++y) {
    for (int x = left; x < left + width; ++x) PaintPixel(image, x, y, color);
  }
}

RgbaColor HalfBlend(RgbaColor foreground, RgbaColor background) {
  return {
      .r = static_cast<uint8_t>((static_cast<int>(foreground.r) + background.r) / 2),
      .g = static_cast<uint8_t>((static_cast<int>(foreground.g) + background.g) / 2),
      .b = static_cast<uint8_t>((static_cast<int>(foreground.b) + background.b) / 2),
      .a = 255,
  };
}

RgbaImage CavePaletteReference() {
  RgbaImage reference = SolidImage(4, 1, RgbaColor{0, 0, 0, 0});
  PaintPixel(reference, 0, 0, RgbaColor{10, 16, 59, 255});
  PaintPixel(reference, 1, 0, RgbaColor{10, 16, 59, 255});
  PaintPixel(reference, 2, 0, RgbaColor{81, 80, 126, 255});
  return reference;
}

GeneratedArtworkPostprocessConfig TestConfig() {
  return {
      .output_width = 8,
      .output_height = 8,
      .background = {255, 0, 255, 255},
      .transparent_distance = 24.0f,
      .opaque_distance = 190.0f,
      .final_alpha_threshold = 100,
      .minimum_visible_pixels = 1,
      .minimum_transparent_border = 1,
  };
}

TEST(GeneratedArtworkPostprocessorTest, PaletteUsesOpaqueReferenceColorsInStableFrequencyOrder) {
  ASSERT_OK_AND_ASSIGN(const std::vector<RgbaColor> palette,
                       ExtractGeneratedArtworkPalette(CavePaletteReference(), 128, 8));

  EXPECT_EQ(palette,
            (std::vector<RgbaColor>{RgbaColor{10, 16, 59, 255}, RgbaColor{81, 80, 126, 255}}));
}

TEST(GeneratedArtworkPostprocessorTest, ConfigValidationNamesTheFailedInvariant) {
  GeneratedArtworkPostprocessConfig config = TestConfig();
  config.output_width = 0;
  absl::Status status = ValidateGeneratedArtworkPostprocessConfig(config);
  EXPECT_NE(status.message().find("output dimensions"), std::string_view::npos);

  config = TestConfig();
  config.opaque_distance = config.transparent_distance;
  status = ValidateGeneratedArtworkPostprocessConfig(config);
  EXPECT_NE(status.message().find("opaque matte distance"), std::string_view::npos);

  config = TestConfig();
  config.alpha_policy = static_cast<GeneratedArtworkAlphaPolicy>(99);
  status = ValidateGeneratedArtworkPostprocessConfig(config);
  EXPECT_NE(status.message().find("alpha policy"), std::string_view::npos);
}

TEST(GeneratedArtworkPostprocessorTest, ChromaFringeBecomesPaletteColorAndBinaryAlpha) {
  const RgbaColor magenta{255, 0, 255, 255};
  const RgbaColor dark{10, 16, 59, 255};
  const RgbaColor highlight{81, 80, 126, 255};
  RgbaImage source = SolidImage(8, 8, magenta);
  PaintRect(source, 3, 3, 2, 2, dark);
  PaintPixel(source, 2, 3, HalfBlend(highlight, magenta));

  ASSERT_OK_AND_ASSIGN(const GeneratedArtworkPostprocessResult result,
                       PostprocessGeneratedArtwork(source, CavePaletteReference(), TestConfig()));

  const size_t fringe = (static_cast<size_t>(3) * result.finished.width + 2) * 4;
  EXPECT_EQ(result.finished.pixels[fringe + 0], highlight.r);
  EXPECT_EQ(result.finished.pixels[fringe + 1], highlight.g);
  EXPECT_EQ(result.finished.pixels[fringe + 2], highlight.b);
  EXPECT_EQ(result.finished.pixels[fringe + 3], 255);
  EXPECT_GT(result.diagnostics.partially_matted_pixels, 0);
  for (size_t pixel = 0;
       pixel < static_cast<size_t>(result.finished.width) * result.finished.height; ++pixel) {
    EXPECT_TRUE(result.finished.pixels[pixel * 4 + 3] == 0 ||
                result.finished.pixels[pixel * 4 + 3] == 255);
  }
}

TEST(GeneratedArtworkPostprocessorTest, ResizePreservesCanvasAndExactReferencePalette) {
  RgbaImage source = SolidImage(8, 8, RgbaColor{255, 0, 255, 255});
  PaintRect(source, 2, 2, 4, 4, RgbaColor{30, 40, 90, 255});
  GeneratedArtworkPostprocessConfig config = TestConfig();
  config.output_width = 4;
  config.output_height = 4;

  ASSERT_OK_AND_ASSIGN(const GeneratedArtworkPostprocessResult result,
                       PostprocessGeneratedArtwork(source, CavePaletteReference(), config));

  EXPECT_EQ(result.finished.width, 4);
  EXPECT_EQ(result.finished.height, 4);
  for (size_t pixel = 0; pixel < 16; ++pixel) {
    const size_t offset = pixel * 4;
    if (result.finished.pixels[offset + 3] == 0) continue;
    const RgbaColor color{result.finished.pixels[offset + 0], result.finished.pixels[offset + 1],
                          result.finished.pixels[offset + 2], 255};
    EXPECT_TRUE(color == RgbaColor(10, 16, 59, 255) || color == RgbaColor(81, 80, 126, 255));
  }
}

TEST(GeneratedArtworkPostprocessorTest, RequiredTransparentBorderRejectsClippedArtwork) {
  RgbaImage source = SolidImage(8, 8, RgbaColor{255, 0, 255, 255});
  PaintRect(source, 0, 2, 4, 4, RgbaColor{10, 16, 59, 255});

  const absl::Status status =
      PostprocessGeneratedArtwork(source, CavePaletteReference(), TestConfig()).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(status.message().find("transparent border"), std::string_view::npos);
}

TEST(GeneratedArtworkPostprocessorTest, RejectsPaletteContainingOnlyTheGeneratedBackground) {
  const RgbaColor magenta{255, 0, 255, 255};
  RgbaImage source = SolidImage(8, 8, magenta);
  PaintRect(source, 2, 2, 4, 4, RgbaColor{10, 16, 59, 255});

  const absl::Status status =
      PostprocessGeneratedArtwork(source, SolidImage(1, 1, magenta), TestConfig()).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(status.message().find("no color distinct"), std::string_view::npos);
}

TEST(GeneratedArtworkPostprocessorTest, MatteRemovalClearsEnclosedConfiguredMatte) {
  const RgbaColor magenta{255, 0, 255, 255};
  const RgbaColor dark{10, 16, 59, 255};
  RgbaImage source = SolidImage(7, 7, magenta);
  PaintRect(source, 1, 1, 5, 5, dark);
  PaintPixel(source, 3, 3, magenta);
  GeneratedArtworkPostprocessConfig config = TestConfig();
  config.output_width = 7;
  config.output_height = 7;

  ASSERT_OK_AND_ASSIGN(const GeneratedArtworkPostprocessResult result,
                       PostprocessGeneratedArtwork(source, CavePaletteReference(), config));

  const size_t enclosed = (static_cast<size_t>(3) * 7 + 3) * 4;
  EXPECT_EQ(result.matted.pixels[enclosed + 0], 0);
  EXPECT_EQ(result.matted.pixels[enclosed + 1], 0);
  EXPECT_EQ(result.matted.pixels[enclosed + 2], 0);
  EXPECT_EQ(result.matted.pixels[enclosed + 3], 0);
}

TEST(GeneratedArtworkPostprocessorTest, MatteRemovalKeepsEnclosedNearMatteWithoutMatchingCore) {
  const RgbaColor magenta{255, 0, 255, 255};
  const RgbaColor dark{10, 16, 59, 255};
  const RgbaColor near_matte{180, 0, 180, 255};
  RgbaImage source = SolidImage(7, 7, magenta);
  PaintRect(source, 1, 1, 5, 5, dark);
  PaintPixel(source, 3, 3, near_matte);
  GeneratedArtworkPostprocessConfig config = TestConfig();
  config.output_width = 7;
  config.output_height = 7;

  ASSERT_OK_AND_ASSIGN(const GeneratedArtworkPostprocessResult result,
                       PostprocessGeneratedArtwork(source, CavePaletteReference(), config));

  const size_t enclosed = (static_cast<size_t>(3) * 7 + 3) * 4;
  EXPECT_EQ(result.matted.pixels[enclosed + 0], near_matte.r);
  EXPECT_EQ(result.matted.pixels[enclosed + 1], near_matte.g);
  EXPECT_EQ(result.matted.pixels[enclosed + 2], near_matte.b);
  EXPECT_EQ(result.matted.pixels[enclosed + 3], 255);
}

TEST(GeneratedArtworkPostprocessorTest, ProcessingIsByteDeterministic) {
  RgbaImage source = SolidImage(8, 8, RgbaColor{255, 0, 255, 255});
  PaintRect(source, 2, 2, 4, 4, RgbaColor{30, 40, 90, 255});

  ASSERT_OK_AND_ASSIGN(const GeneratedArtworkPostprocessResult first,
                       PostprocessGeneratedArtwork(source, CavePaletteReference(), TestConfig()));
  ASSERT_OK_AND_ASSIGN(const GeneratedArtworkPostprocessResult second,
                       PostprocessGeneratedArtwork(source, CavePaletteReference(), TestConfig()));

  EXPECT_EQ(first.finished.pixels, second.finished.pixels);
  EXPECT_EQ(first.palette, second.palette);
  EXPECT_EQ(first.diagnostics.visible_pixels, second.diagnostics.visible_pixels);
}

TEST(GeneratedArtworkPostprocessorTest, PreservePoliciesNeedNoPaletteAndKeepPartialAlpha) {
  RgbaImage source = SolidImage(2, 2, RgbaColor{12, 34, 56, 128});
  PaintPixel(source, 0, 0, RgbaColor{99, 88, 77, 0});
  GeneratedArtworkPostprocessConfig config{
      .output_width = 2,
      .output_height = 2,
      .background_policy = GeneratedArtworkBackgroundPolicy::kPreserve,
      .palette_policy = GeneratedArtworkPalettePolicy::kPreserve,
      .alpha_policy = GeneratedArtworkAlphaPolicy::kPreserve,
      .minimum_visible_pixels = 1,
      .minimum_transparent_border = 0,
  };

  ASSERT_OK_AND_ASSIGN(const GeneratedArtworkPostprocessResult result,
                       PostprocessGeneratedArtwork(source, std::vector<RgbaColor>{}, config));

  EXPECT_EQ(result.finished.pixels[3], 0);
  EXPECT_EQ(result.finished.pixels[0], 0);
  EXPECT_EQ(result.finished.pixels[1], 0);
  EXPECT_EQ(result.finished.pixels[2], 0);
  EXPECT_EQ(result.finished.pixels[7], 128);
  EXPECT_TRUE(result.palette.empty());
}

TEST(GeneratedArtworkPostprocessorTest, OpaquePolicyFillsAlphaAndAllowsArtworkAtEdges) {
  RgbaImage source = SolidImage(2, 2, RgbaColor{12, 34, 56, 0});
  GeneratedArtworkPostprocessConfig config{
      .output_width = 2,
      .output_height = 2,
      .background_policy = GeneratedArtworkBackgroundPolicy::kPreserve,
      .palette_policy = GeneratedArtworkPalettePolicy::kPreserve,
      .alpha_policy = GeneratedArtworkAlphaPolicy::kOpaque,
      .minimum_visible_pixels = 4,
      .minimum_transparent_border = 1,
  };

  ASSERT_OK_AND_ASSIGN(const GeneratedArtworkPostprocessResult result,
                       PostprocessGeneratedArtwork(source, std::vector<RgbaColor>{}, config));

  EXPECT_EQ(result.diagnostics.visible_pixels, 4);
  for (size_t offset = 3; offset < result.finished.pixels.size(); offset += 4) {
    EXPECT_EQ(result.finished.pixels[offset], 255);
  }
}

TEST(GeneratedArtworkPostprocessorTest, PixelBlockSizeProducesConstantColorBlocks) {
  RgbaImage source = SolidImage(4, 4, RgbaColor{0, 0, 0, 255});
  PaintRect(source, 2, 0, 2, 4, RgbaColor{255, 255, 255, 255});
  GeneratedArtworkPostprocessConfig config{
      .output_width = 4,
      .output_height = 4,
      .pixel_block_size = 2,
      .background_policy = GeneratedArtworkBackgroundPolicy::kPreserve,
      .palette_policy = GeneratedArtworkPalettePolicy::kPreserve,
      .alpha_policy = GeneratedArtworkAlphaPolicy::kOpaque,
      .minimum_visible_pixels = 1,
      .minimum_transparent_border = 0,
  };

  ASSERT_OK_AND_ASSIGN(const GeneratedArtworkPostprocessResult result,
                       PostprocessGeneratedArtwork(source, std::vector<RgbaColor>{}, config));

  for (int block_y = 0; block_y < 4; block_y += 2) {
    for (int block_x = 0; block_x < 4; block_x += 2) {
      const size_t first = (static_cast<size_t>(block_y) * 4 + block_x) * 4;
      for (int y = block_y; y < block_y + 2; ++y) {
        for (int x = block_x; x < block_x + 2; ++x) {
          const size_t pixel = (static_cast<size_t>(y) * 4 + x) * 4;
          EXPECT_EQ(std::vector<uint8_t>(result.finished.pixels.begin() + pixel,
                                         result.finished.pixels.begin() + pixel + 4),
                    std::vector<uint8_t>(result.finished.pixels.begin() + first,
                                         result.finished.pixels.begin() + first + 4));
        }
      }
    }
  }
}

}  // namespace
}  // namespace zebes
