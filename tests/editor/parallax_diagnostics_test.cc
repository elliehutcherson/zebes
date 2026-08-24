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
      .scroll_factor = {0.5, 0.25},
      .offset = {0, 0},
  };

  ASSERT_OK_AND_ASSIGN(const CameraCoverageDiagnostics diagnostics,
                       AnalyzeCameraCoverage(layer, {.min = {0, 0}, .max = {400, 300}}, {100, 100},
                                             {300, 200}, {320, 180}, {0.5, 2.0}));

  EXPECT_DOUBLE_EQ(diagnostics.horizontal.minimum_start_margin, -110.0);
  EXPECT_DOUBLE_EQ(diagnostics.horizontal.minimum_end_margin, -230.0);
  EXPECT_FALSE(diagnostics.horizontal.covers());
  EXPECT_DOUBLE_EQ(diagnostics.vertical.minimum_start_margin, -20.0);
  EXPECT_DOUBLE_EQ(diagnostics.vertical.minimum_end_margin, -65.0);
  EXPECT_FALSE(diagnostics.vertical.covers());
}

TEST(ParallaxDiagnosticsTest, RepeatedAxesAreCoveredWithoutMeaninglessMargins) {
  ParallaxLayer layer{.repeat_period = {1, 1}};

  ASSERT_OK_AND_ASSIGN(const CameraCoverageDiagnostics diagnostics,
                       AnalyzeCameraCoverage(layer, {.min = {0, 0}, .max = {1, 1}}, {0, 0},
                                             {1000, 1000}, {320, 180}, {0.5, 2.0}));

  EXPECT_TRUE(diagnostics.horizontal.covers());
  EXPECT_TRUE(diagnostics.vertical.covers());
  EXPECT_DOUBLE_EQ(diagnostics.horizontal.minimum_start_margin, 0.0);
  EXPECT_DOUBLE_EQ(diagnostics.vertical.minimum_end_margin, 0.0);
}

TEST(ParallaxDiagnosticsTest, ReportsRepeatGapAndOverlapFromCompositionBounds) {
  ParallaxLayer layer{.repeat_period = {120, 80}};

  ASSERT_OK_AND_ASSIGN(const CameraCoverageDiagnostics diagnostics,
                       AnalyzeCameraCoverage(layer, {.min = {-10, 5}, .max = {90, 105}}, {0, 0},
                                             {0, 0}, {320, 180}, {1.0, 1.0}));

  EXPECT_DOUBLE_EQ(diagnostics.horizontal.composition_span, 100);
  EXPECT_DOUBLE_EQ(diagnostics.horizontal.period_minus_span, 20);
  EXPECT_DOUBLE_EQ(diagnostics.vertical.composition_span, 100);
  EXPECT_DOUBLE_EQ(diagnostics.vertical.period_minus_span, -20);
}

TEST(ParallaxDiagnosticsTest, ReportsAdjacentAndFirstLastWrapSeams) {
  ParallaxLayer layer{
      .repeat_period = {120, 0},
      .elements =
          {
              {.id = 1, .name = "A", .texture_id = "a", .position = {0, 0}},
              {.id = 2, .name = "B", .texture_id = "b", .position = {45, 0}},
          },
  };

  ASSERT_OK_AND_ASSIGN(
      const CompositionSeamDiagnostics diagnostics,
      AnalyzeCompositionSeams(layer, {{.element_id = 1, .width = 40, .height = 20},
                                      {.element_id = 2, .width = 50, .height = 20}}));

  ASSERT_EQ(diagnostics.adjacent.size(), 1);
  EXPECT_DOUBLE_EQ(diagnostics.adjacent[0].separation.x, 5);
  ASSERT_TRUE(diagnostics.horizontal_wrap.has_value());
  EXPECT_DOUBLE_EQ(diagnostics.horizontal_wrap->separation.x, 25);
  EXPECT_FALSE(diagnostics.vertical_wrap.has_value());
}

TEST(ParallaxDiagnosticsTest, LevelContextClampsWorldCornersToReachableCameraCenters) {
  const ParallaxLayer layer{
      .scroll_factor = {1.0, 1.0},
  };

  ASSERT_OK_AND_ASSIGN(const CameraCoverageDiagnostics diagnostics,
                       AnalyzeCameraCoverage(layer, {.min = {0, 0}, .max = {960, 540}}, {0, 0},
                                             {960, 540}, {960, 540}, {1.0, 1.0},
                                             CameraWorldBounds{.min = {0, 0}, .max = {960, 540}}));

  EXPECT_DOUBLE_EQ(diagnostics.horizontal.minimum_start_margin, 0.0);
  EXPECT_DOUBLE_EQ(diagnostics.horizontal.minimum_end_margin, 0.0);
  EXPECT_DOUBLE_EQ(diagnostics.vertical.minimum_start_margin, 0.0);
  EXPECT_DOUBLE_EQ(diagnostics.vertical.minimum_end_margin, 0.0);
}

}  // namespace
}  // namespace zebes
