#include "editor/parallax_theme_editor/parallax_diagnostics.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

RgbaImage Image(int width, int height, std::vector<uint8_t> pixels) {
  return RgbaImage{.width = width, .height = height, .pixels = std::move(pixels)};
}

TEST(ParallaxDiagnosticsTest, ReportsMeasuredOpposingEdgeDifferences) {
  const RgbaImage image = Image(2, 2,
                                {
                                    0,
                                    10,
                                    20,
                                    255,
                                    10,
                                    10,
                                    20,
                                    255,
                                    30,
                                    40,
                                    50,
                                    255,
                                    30,
                                    60,
                                    50,
                                    255,
                                });

  ASSERT_OK_AND_ASSIGN(const RepetitionDiagnostics diagnostics, AnalyzeRepetition(image));

  EXPECT_EQ(diagnostics.horizontal.pixels_compared, 2);
  EXPECT_EQ(diagnostics.horizontal.exact_pixel_matches, 0);
  EXPECT_DOUBLE_EQ(diagnostics.horizontal.mean_absolute_channel_difference, 3.75);
  EXPECT_EQ(diagnostics.horizontal.maximum_channel_difference, 20);
  EXPECT_EQ(diagnostics.vertical.pixels_compared, 2);
  EXPECT_EQ(diagnostics.vertical.exact_pixel_matches, 0);
  EXPECT_DOUBLE_EQ(diagnostics.vertical.mean_absolute_channel_difference, 23.75);
  EXPECT_EQ(diagnostics.vertical.maximum_channel_difference, 50);
}

TEST(ParallaxDiagnosticsTest, RejectsInvalidImages) {
  EXPECT_EQ(AnalyzeRepetition({}).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ParallaxDiagnosticsTest, ReportsCoverageMarginsAcrossRouteAndZoomExtremes) {
  const ParallaxLayer layer{
      .repeat_x = false,
      .repeat_y = false,
      .scroll_factor = {0.5, 0.25},
      .offset = {0, 0},
      .base_scale = 2.0f,
  };

  ASSERT_OK_AND_ASSIGN(
      const CameraCoverageDiagnostics diagnostics,
      AnalyzeCameraCoverage(layer, 200, 150, {100, 100}, {300, 200}, {320, 180}, {0.5, 2.0}));

  EXPECT_DOUBLE_EQ(diagnostics.horizontal.minimum_start_margin, -110.0);
  EXPECT_DOUBLE_EQ(diagnostics.horizontal.minimum_end_margin, -230.0);
  EXPECT_FALSE(diagnostics.horizontal.covers());
  EXPECT_DOUBLE_EQ(diagnostics.vertical.minimum_start_margin, -20.0);
  EXPECT_DOUBLE_EQ(diagnostics.vertical.minimum_end_margin, -65.0);
  EXPECT_FALSE(diagnostics.vertical.covers());
}

TEST(ParallaxDiagnosticsTest, RepeatedAxesAreCoveredWithoutMeaninglessMargins) {
  ParallaxLayer layer{.repeat_x = true, .repeat_y = true, .base_scale = 1.0f};

  ASSERT_OK_AND_ASSIGN(
      const CameraCoverageDiagnostics diagnostics,
      AnalyzeCameraCoverage(layer, 1, 1, {0, 0}, {1000, 1000}, {320, 180}, {0.5, 2.0}));

  EXPECT_TRUE(diagnostics.horizontal.covers());
  EXPECT_TRUE(diagnostics.vertical.covers());
  EXPECT_DOUBLE_EQ(diagnostics.horizontal.minimum_start_margin, 0.0);
  EXPECT_DOUBLE_EQ(diagnostics.vertical.minimum_end_margin, 0.0);
}

TEST(ParallaxDiagnosticsTest, LevelContextClampsWorldCornersToReachableCameraCenters) {
  const ParallaxLayer layer{
      .repeat_x = false,
      .repeat_y = false,
      .scroll_factor = {1.0, 1.0},
      .base_scale = 1.0f,
  };

  ASSERT_OK_AND_ASSIGN(
      const CameraCoverageDiagnostics diagnostics,
      AnalyzeCameraCoverage(layer, 960, 540, {0, 0}, {960, 540}, {960, 540}, {1.0, 1.0},
                            CameraWorldBounds{.min = {0, 0}, .max = {960, 540}}));

  EXPECT_DOUBLE_EQ(diagnostics.horizontal.minimum_start_margin, 0.0);
  EXPECT_DOUBLE_EQ(diagnostics.horizontal.minimum_end_margin, 0.0);
  EXPECT_DOUBLE_EQ(diagnostics.vertical.minimum_start_margin, 0.0);
  EXPECT_DOUBLE_EQ(diagnostics.vertical.minimum_end_margin, 0.0);
}

}  // namespace
}  // namespace zebes
