#include "artwork/animation_frame_set_pipeline.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

constexpr RgbaColor kTransparent{0, 0, 0, 0};
constexpr RgbaColor kMatte{255, 0, 255, 255};
constexpr RgbaColor kBody{32, 64, 128, 255};
constexpr RgbaColor kHighlight{96, 144, 192, 255};

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

void PaintRect(RgbaImage& image, int left, int top, int width, int height, RgbaColor color) {
  for (int y = top; y < top + height; ++y) {
    for (int x = left; x < left + width; ++x) PaintPixel(image, x, y, color);
  }
}

AnimationFrameSetSheetLayout TestLayout() {
  return {
      .grid_x = 1,
      .grid_y = 1,
      .cell_width = 6,
      .cell_height = 6,
      .column_gap = 1,
      .row_gap = 1,
      .columns = 2,
      .rows = 2,
  };
}

AnimationFrameSetStyle MatteStyle() {
  return {
      .extraction = AnimationFrameSetExtraction::kRemoveSolidMatte,
      .matte = kMatte,
      .palette = {kBody, kHighlight},
  };
}

AnimationFrameSetPipelineConfig TestConfig() {
  return {
      .sheet = TestLayout(),
      .output_width = 12,
      .output_height = 12,
      .origin_x = 6,
      .origin_y = 10,
      .contact_line_y = 10,
      .render_scale = 2,
      .contact_tolerance = 2,
      .minimum_visible_pixels = 4,
      .maximum_horizontal_anchor_drift = 4,
      .maximum_vertical_anchor_drift = 10,
      .packing_columns = 2,
      .playback_mode = SpritePlaybackMode::kHoldLast,
      .frames_per_cycle = {3, 4, 5, 6},
      .planted_frames = {true, true, true, true},
  };
}

RgbaImage PaintedMatteSheet() {
  RgbaImage source = SolidImage(15, 15, kMatte);
  const AnimationFrameSetSheetLayout layout = TestLayout();
  for (int index = 0; index < 4; ++index) {
    const int cell_x =
        layout.grid_x + (index % layout.columns) * (layout.cell_width + layout.column_gap);
    const int cell_y =
        layout.grid_y + (index / layout.columns) * (layout.cell_height + layout.row_gap);
    const int subject_x = 2 + index % 2;
    PaintRect(source, cell_x + subject_x, cell_y + 4, 2, 1,
              index % 2 == 0 ? RgbaColor{34, 66, 130, 255} : RgbaColor{94, 142, 190, 255});
    if (index >= 2) {
      PaintPixel(source, cell_x + subject_x, cell_y + 3, index % 2 == 0 ? kBody : kHighlight);
    }
  }
  return source;
}

void ExpectBinaryPaletteAndCleanTransparency(const RgbaImage& image) {
  for (size_t offset = 0; offset < image.pixels.size(); offset += 4) {
    const RgbaColor color{
        .r = image.pixels[offset + 0],
        .g = image.pixels[offset + 1],
        .b = image.pixels[offset + 2],
        .a = image.pixels[offset + 3],
    };
    if (color.a == 0) {
      EXPECT_EQ(color, kTransparent);
      continue;
    }
    EXPECT_EQ(color.a, 255);
    EXPECT_TRUE(color == kBody || color == kHighlight);
  }
}

TEST(AnimationFrameSetPipelineTest,
     ProcessesSharedRegistrationPaletteTimingPlaybackAndGridPacking) {
  const AnimationFrameSetStyle style = MatteStyle();
  const AnimationFrameSetPipelineConfig config = TestConfig();
  const RgbaImage source = PaintedMatteSheet();

  ASSERT_OK_AND_ASSIGN(const AnimationFrameSetPipelineResult result,
                       RunAnimationFrameSetPipeline(source, style, config));

  EXPECT_EQ(result.pipeline_version, kAnimationFrameSetPipelineVersion);
  EXPECT_FALSE(result.source_digest.empty());
  EXPECT_FALSE(result.packed_digest.empty());
  EXPECT_EQ(result.playback_mode, SpritePlaybackMode::kHoldLast);
  ASSERT_EQ(result.frames.size(), 4);
  EXPECT_EQ(result.packed_texture.width, 24);
  EXPECT_EQ(result.packed_texture.height, 24);
  ASSERT_EQ(result.sprite_frames.size(), 4);

  for (int index = 0; index < 4; ++index) {
    const SpriteFrame expected{
        .index = index,
        .texture_x = (index % 2) * 12,
        .texture_y = (index / 2) * 12,
        .texture_w = 12,
        .texture_h = 12,
        .render_w = 24,
        .render_h = 24,
        .frames_per_cycle = config.frames_per_cycle[static_cast<size_t>(index)],
        .offset_x = -12,
        .offset_y = -20,
    };
    EXPECT_EQ(result.sprite_frames[static_cast<size_t>(index)], expected);
    EXPECT_EQ(result.frames[static_cast<size_t>(index)].extracted.width, 6);
    EXPECT_EQ(result.frames[static_cast<size_t>(index)].rasterized.width, 12);
    EXPECT_TRUE(result.frames[static_cast<size_t>(index)].diagnostics.contact_line_hit);
    EXPECT_EQ(result.frames[static_cast<size_t>(index)].diagnostics.vertical_anchor_drift, 0);
    ExpectBinaryPaletteAndCleanTransparency(result.frames[static_cast<size_t>(index)].finished);
  }
  ExpectBinaryPaletteAndCleanTransparency(result.packed_texture);
}

TEST(AnimationFrameSetPipelineTest, PreserveAlphaClearsTransparentRgb) {
  AnimationFrameSetPipelineConfig config = TestConfig();
  config.sheet = {
      .grid_x = 0,
      .grid_y = 0,
      .cell_width = 6,
      .cell_height = 6,
      .column_gap = 0,
      .row_gap = 0,
      .columns = 1,
      .rows = 1,
  };
  config.packing_columns = 1;
  config.frames_per_cycle = {7};
  config.planted_frames = {true};
  const AnimationFrameSetStyle style{
      .extraction = AnimationFrameSetExtraction::kPreserveAlpha,
      .palette = {kBody},
  };
  RgbaImage source = SolidImage(6, 6, RgbaColor{200, 100, 50, 0});
  PaintRect(source, 2, 4, 2, 1, kBody);

  ASSERT_OK_AND_ASSIGN(const AnimationFrameSetPipelineResult result,
                       RunAnimationFrameSetPipeline(source, style, config));

  ASSERT_EQ(result.frames.size(), 1);
  ExpectBinaryPaletteAndCleanTransparency(result.frames[0].finished);
}

TEST(AnimationFrameSetPipelineTest, RejectsForegroundInGuttersAndMargins) {
  const AnimationFrameSetStyle style = MatteStyle();
  const AnimationFrameSetPipelineConfig config = TestConfig();
  RgbaImage source = PaintedMatteSheet();
  PaintPixel(source, 7, 3, kBody);

  const absl::Status status = RunAnimationFrameSetPipeline(source, style, config).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("outside declared cells"), std::string::npos);
}

TEST(AnimationFrameSetPipelineTest, RejectsMissingAndClippedFrames) {
  const AnimationFrameSetStyle style = MatteStyle();
  const AnimationFrameSetPipelineConfig config = TestConfig();
  RgbaImage missing = PaintedMatteSheet();
  const AnimationFrameSetSheetLayout layout = TestLayout();
  const int last_x = layout.grid_x + layout.cell_width + layout.column_gap;
  const int last_y = layout.grid_y + layout.cell_height + layout.row_gap;
  PaintRect(missing, last_x, last_y, layout.cell_width, layout.cell_height, kMatte);

  const absl::Status missing_status = RunAnimationFrameSetPipeline(missing, style, config).status();
  EXPECT_EQ(missing_status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(missing_status.message().find("visible pixels"), std::string::npos);

  RgbaImage clipped = PaintedMatteSheet();
  PaintRect(clipped, layout.grid_x, layout.grid_y + 3, 1, 2, kBody);
  const absl::Status clipped_status = RunAnimationFrameSetPipeline(clipped, style, config).status();
  EXPECT_EQ(clipped_status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(clipped_status.message().find("transparent border"), std::string::npos);
}

TEST(AnimationFrameSetPipelineTest, RejectsNonUniformScaleAndInvalidPacking) {
  const AnimationFrameSetStyle style = MatteStyle();
  AnimationFrameSetPipelineConfig config = TestConfig();
  config.output_height = 10;
  config.origin_y = 9;
  config.contact_line_y = 9;
  EXPECT_EQ(ValidateAnimationFrameSetPipelineConfig(config, style).code(),
            absl::StatusCode::kInvalidArgument);

  config = TestConfig();
  config.packing_columns = 3;
  EXPECT_EQ(ValidateAnimationFrameSetPipelineConfig(config, style).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(AnimationFrameSetPipelineTest, RejectsContactAndAnchorDriftViolations) {
  const AnimationFrameSetStyle style = MatteStyle();
  AnimationFrameSetPipelineConfig config = TestConfig();
  config.origin_y = 8;
  config.contact_line_y = 8;
  config.planted_frames.assign(4, false);

  const absl::Status contact_status =
      RunAnimationFrameSetPipeline(PaintedMatteSheet(), style, config).status();
  EXPECT_EQ(contact_status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(contact_status.message().find("below the contact line"), std::string::npos);

  config = TestConfig();
  config.maximum_horizontal_anchor_drift = 0;
  const absl::Status drift_status =
      RunAnimationFrameSetPipeline(PaintedMatteSheet(), style, config).status();
  EXPECT_EQ(drift_status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(drift_status.message().find("anchor drift"), std::string::npos);
}

TEST(AnimationFrameSetPipelineTest, RepeatingSameInputIsByteStable) {
  const AnimationFrameSetStyle style = MatteStyle();
  const AnimationFrameSetPipelineConfig config = TestConfig();
  const RgbaImage source = PaintedMatteSheet();

  ASSERT_OK_AND_ASSIGN(const AnimationFrameSetPipelineResult first,
                       RunAnimationFrameSetPipeline(source, style, config));
  ASSERT_OK_AND_ASSIGN(const AnimationFrameSetPipelineResult second,
                       RunAnimationFrameSetPipeline(source, style, config));

  EXPECT_EQ(first.source_digest, second.source_digest);
  EXPECT_EQ(first.packed_digest, second.packed_digest);
  EXPECT_EQ(first.packed_texture.width, second.packed_texture.width);
  EXPECT_EQ(first.packed_texture.height, second.packed_texture.height);
  EXPECT_EQ(first.packed_texture.pixels, second.packed_texture.pixels);
  EXPECT_EQ(first.sprite_frames, second.sprite_frames);
  ASSERT_EQ(first.frames.size(), second.frames.size());
  for (size_t index = 0; index < first.frames.size(); ++index) {
    EXPECT_EQ(first.frames[index].finished.pixels, second.frames[index].finished.pixels);
    EXPECT_EQ(first.frames[index].diagnostics, second.frames[index].diagnostics);
  }
}

}  // namespace
}  // namespace zebes
