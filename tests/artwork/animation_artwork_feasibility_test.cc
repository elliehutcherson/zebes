#include "artwork/animation_artwork_feasibility.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "common/image_digest.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

constexpr int kCellSize = 12;
constexpr RgbaColor kMatte{255, 0, 255, 255};
constexpr RgbaColor kBody{32, 64, 128, 255};
constexpr RgbaColor kHighlight{96, 144, 192, 255};

RgbaImage MatteSheet(int columns, int rows) {
  RgbaImage sheet{
      .width = columns * kCellSize,
      .height = rows * kCellSize,
      .pixels =
          std::vector<uint8_t>(static_cast<size_t>(columns * kCellSize) * rows * kCellSize * 4),
  };
  for (size_t offset = 0; offset < sheet.pixels.size(); offset += 4) {
    sheet.pixels[offset + 0] = kMatte.r;
    sheet.pixels[offset + 1] = kMatte.g;
    sheet.pixels[offset + 2] = kMatte.b;
    sheet.pixels[offset + 3] = kMatte.a;
  }
  return sheet;
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

void PaintFrame(RgbaImage& sheet, int index, int columns) {
  const int cell_left = (index % columns) * kCellSize;
  const int cell_top = (index / columns) * kCellSize;
  // The lower two source rows become output rows 36..43 after the shared 4x
  // resize.  They therefore touch the contact band without crossing it.
  const int subject_left = 2 + (index % 5);
  PaintRect(sheet, cell_left + subject_left, cell_top + 9, 4, 2, kBody);
  // This connected shoulder changes the silhouette while leaving a clear
  // matte guard band around every cell.
  PaintRect(sheet, cell_left + subject_left + 1, cell_top + 8, 2, 1, kHighlight);
}

void CopyFrame(RgbaImage& sheet, int from, int to, int columns) {
  const int from_left = (from % columns) * kCellSize;
  const int from_top = (from / columns) * kCellSize;
  const int to_left = (to % columns) * kCellSize;
  const int to_top = (to / columns) * kCellSize;
  for (int y = 0; y < kCellSize; ++y) {
    for (int x = 0; x < kCellSize; ++x) {
      const size_t source = (static_cast<size_t>(from_top + y) * sheet.width + from_left + x) * 4;
      const size_t destination = (static_cast<size_t>(to_top + y) * sheet.width + to_left + x) * 4;
      std::copy_n(sheet.pixels.begin() + static_cast<ptrdiff_t>(source), 4,
                  sheet.pixels.begin() + static_cast<ptrdiff_t>(destination));
    }
  }
}

AnimationArtworkFeasibilityConfig SyntheticConfig(AnimationFeasibilityClip clip, int columns,
                                                  int rows) {
  const AnimationFeasibilitySheetLayout sheet{
      .grid_x = 0,
      .grid_y = 0,
      .cell_width = kCellSize,
      .cell_height = kCellSize,
      .column_gap = 0,
      .row_gap = 0,
      .columns = columns,
      .rows = rows,
  };
  const absl::StatusOr<AnimationArtworkFeasibilityConfig> config =
      MakeAnimationArtworkFeasibilityConfig(clip, sheet, {kBody, kHighlight});
  EXPECT_TRUE(config.ok()) << config.status();
  return *config;
}

void ExpectSameImage(const RgbaImage& left, const RgbaImage& right) {
  EXPECT_EQ(left.width, right.width);
  EXPECT_EQ(left.height, right.height);
  EXPECT_EQ(left.pixels, right.pixels);
}

void ExpectSameDiagnostics(const AnimationFeasibilityFrameDiagnostics& left,
                           const AnimationFeasibilityFrameDiagnostics& right) {
  EXPECT_EQ(left.index, right.index);
  EXPECT_EQ(left.bounds, right.bounds);
  EXPECT_EQ(left.visible_pixels, right.visible_pixels);
  EXPECT_EQ(left.contact_line_hit, right.contact_line_hit);
}

TEST(AnimationArtworkFeasibilityTest, IdleSuccessHasExactPackingFramesAndDigests) {
  const AnimationArtworkFeasibilityConfig config =
      SyntheticConfig(AnimationFeasibilityClip::kIdleRight, 2, 2);
  RgbaImage source = MatteSheet(2, 2);
  for (int index = 0; index < 4; ++index) PaintFrame(source, index, 2);

  ASSERT_OK_AND_ASSIGN(const AnimationArtworkFeasibilityResult result,
                       RunAnimationArtworkFeasibility(source, config));
  ASSERT_OK_AND_ASSIGN(const std::string expected_source_digest, RgbaImageDigest(source));
  ASSERT_OK_AND_ASSIGN(const std::string expected_packed_digest,
                       RgbaImageDigest(result.packed_texture));

  EXPECT_EQ(result.source_digest, expected_source_digest);
  EXPECT_EQ(result.packed_digest, expected_packed_digest);
  EXPECT_EQ(result.frames.size(), 4);
  EXPECT_EQ(result.packed_texture.width, 192);
  EXPECT_EQ(result.packed_texture.height, 44);
  EXPECT_EQ(result.sprite_frames.size(), 4);

  for (int index = 0; index < 4; ++index) {
    const SpriteFrame expected{
        .index = index,
        .texture_x = index * 48,
        .texture_y = 0,
        .texture_w = 48,
        .texture_h = 44,
        .render_w = 96,
        .render_h = 88,
        .frames_per_cycle = 15,
        .offset_x = -48,
        .offset_y = -88,
    };
    EXPECT_EQ(result.sprite_frames[index], expected);
    EXPECT_EQ(result.frames[index].finished.width, 48);
    EXPECT_EQ(result.frames[index].finished.height, 48);
    EXPECT_TRUE(result.frames[index].diagnostics.contact_line_hit);
  }
}

TEST(AnimationArtworkFeasibilityTest, LocomotionUsesTenFramesAndFixedTiming) {
  const AnimationArtworkFeasibilityConfig config =
      SyntheticConfig(AnimationFeasibilityClip::kLocomotionRight, 5, 2);
  RgbaImage source = MatteSheet(5, 2);
  for (int index = 0; index < 10; ++index) PaintFrame(source, index, 5);

  ASSERT_OK_AND_ASSIGN(const AnimationArtworkFeasibilityResult result,
                       RunAnimationArtworkFeasibility(source, config));

  EXPECT_EQ(result.frames.size(), 10);
  EXPECT_EQ(result.packed_texture.width, 480);
  EXPECT_EQ(result.packed_texture.height, 44);
  ASSERT_EQ(result.sprite_frames.size(), 10);
  for (const SpriteFrame& frame : result.sprite_frames) {
    EXPECT_EQ(frame.frames_per_cycle, 4);
    EXPECT_EQ(frame.render_w, 96);
    EXPECT_EQ(frame.render_h, 88);
  }
}

TEST(AnimationArtworkFeasibilityTest, AuthoredTwelveFrameLocomotionPacksEveryFrame) {
  const AnimationFeasibilitySheetLayout sheet{
      .grid_x = 0,
      .grid_y = 0,
      .cell_width = kCellSize,
      .cell_height = kCellSize,
      .column_gap = 0,
      .row_gap = 0,
      .columns = 6,
      .rows = 2,
  };
  const std::vector<bool> planted = {
      true, false, false, false, false, false, true, false, false, false, false, false,
  };
  ASSERT_OK_AND_ASSIGN(const AnimationArtworkFeasibilityConfig config,
                       MakeAuthoredAnimationArtworkFeasibilityConfig(
                           AnimationFeasibilityClip::kLocomotionRight, sheet, {kBody, kHighlight},
                           std::vector<int>(12, 3), planted));
  RgbaImage source = MatteSheet(6, 2);
  for (int index = 0; index < 12; ++index) PaintFrame(source, index, 6);

  ASSERT_OK_AND_ASSIGN(const AnimationArtworkFeasibilityResult result,
                       RunAnimationArtworkFeasibility(source, config));

  EXPECT_EQ(config.planted_frames, planted);
  EXPECT_EQ(result.frames.size(), 12);
  EXPECT_EQ(result.packed_texture.width, 576);
  EXPECT_EQ(result.packed_texture.height, 44);
  ASSERT_EQ(result.sprite_frames.size(), 12);
  for (int index = 0; index < 12; ++index) {
    EXPECT_EQ(result.sprite_frames[index].index, index);
    EXPECT_EQ(result.sprite_frames[index].texture_x, index * 48);
    EXPECT_EQ(result.sprite_frames[index].frames_per_cycle, 3);
  }
}

TEST(AnimationArtworkFeasibilityTest, AuthoredConfigPreservesOrderedTiming) {
  const AnimationFeasibilitySheetLayout sheet{
      .grid_x = 0,
      .grid_y = 0,
      .cell_width = kCellSize,
      .cell_height = kCellSize,
      .column_gap = 0,
      .row_gap = 0,
      .columns = 6,
      .rows = 2,
  };
  const std::vector<int> timing = {3, 3, 4, 3, 3, 4, 3, 3, 4, 3, 3, 4};
  const std::vector<bool> planted = {
      true, true, false, false, false, false, true, true, false, false, false, false,
  };

  ASSERT_OK_AND_ASSIGN(
      const AnimationArtworkFeasibilityConfig config,
      MakeAuthoredAnimationArtworkFeasibilityConfig(AnimationFeasibilityClip::kLocomotionRight,
                                                    sheet, {kBody, kHighlight}, timing, planted));

  EXPECT_EQ(config.frames_per_cycle, timing);
  EXPECT_EQ(config.planted_frames, planted);
}

TEST(AnimationArtworkFeasibilityTest, AuthoredConfigRejectsMismatchedTimingAndContact) {
  const AnimationFeasibilitySheetLayout sheet{
      .grid_x = 0,
      .grid_y = 0,
      .cell_width = kCellSize,
      .cell_height = kCellSize,
      .column_gap = 0,
      .row_gap = 0,
      .columns = 6,
      .rows = 2,
  };

  EXPECT_EQ(MakeAuthoredAnimationArtworkFeasibilityConfig(
                AnimationFeasibilityClip::kLocomotionRight, sheet, {kBody, kHighlight},
                std::vector<int>(11, 3), std::vector<bool>(12, false))
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(MakeAuthoredAnimationArtworkFeasibilityConfig(
                AnimationFeasibilityClip::kLocomotionRight, sheet, {kBody, kHighlight},
                std::vector<int>(12, 3), std::vector<bool>(11, false))
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(AnimationArtworkFeasibilityTest, RejectsInvalidGridAndSourceBounds) {
  AnimationArtworkFeasibilityConfig invalid =
      SyntheticConfig(AnimationFeasibilityClip::kIdleRight, 2, 2);
  invalid.sheet.columns = 0;
  EXPECT_EQ(ValidateAnimationArtworkFeasibilityConfig(invalid).code(),
            absl::StatusCode::kInvalidArgument);

  const AnimationArtworkFeasibilityConfig config =
      SyntheticConfig(AnimationFeasibilityClip::kIdleRight, 2, 2);
  RgbaImage source = MatteSheet(2, 2);
  for (int index = 0; index < 4; ++index) PaintFrame(source, index, 2);
  source.width -= 1;
  source.pixels.resize(static_cast<size_t>(source.width) * source.height * 4);
  const absl::Status status = RunAnimationArtworkFeasibility(source, config).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("extend beyond"), std::string::npos);
}

TEST(AnimationArtworkFeasibilityTest, RejectsInvalidPaletteAndTiming) {
  AnimationArtworkFeasibilityConfig invalid =
      SyntheticConfig(AnimationFeasibilityClip::kIdleRight, 2, 2);
  invalid.palette.clear();
  EXPECT_EQ(ValidateAnimationArtworkFeasibilityConfig(invalid).code(),
            absl::StatusCode::kInvalidArgument);

  invalid = SyntheticConfig(AnimationFeasibilityClip::kIdleRight, 2, 2);
  invalid.palette[1] = invalid.palette[0];
  EXPECT_EQ(ValidateAnimationArtworkFeasibilityConfig(invalid).code(),
            absl::StatusCode::kInvalidArgument);

  invalid = SyntheticConfig(AnimationFeasibilityClip::kIdleRight, 2, 2);
  invalid.frames_per_cycle[0] = 0;
  EXPECT_EQ(ValidateAnimationArtworkFeasibilityConfig(invalid).code(),
            absl::StatusCode::kInvalidArgument);

  invalid = SyntheticConfig(AnimationFeasibilityClip::kIdleRight, 2, 2);
  invalid.planted_frames.pop_back();
  EXPECT_EQ(ValidateAnimationArtworkFeasibilityConfig(invalid).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(AnimationArtworkFeasibilityTest, RejectsForegroundBelowContactLine) {
  AnimationArtworkFeasibilityConfig config =
      SyntheticConfig(AnimationFeasibilityClip::kIdleRight, 2, 2);
  config.contact_line_y = 40;
  config.origin_y = 40;
  config.planted_frames.assign(4, false);
  RgbaImage source = MatteSheet(2, 2);
  for (int index = 0; index < 4; ++index) PaintFrame(source, index, 2);

  const absl::Status status = RunAnimationArtworkFeasibility(source, config).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(status.message().find("below the contact line"), std::string::npos);
}

TEST(AnimationArtworkFeasibilityTest, RejectsPlantedFrameMissingContact) {
  const AnimationArtworkFeasibilityConfig config =
      SyntheticConfig(AnimationFeasibilityClip::kIdleRight, 2, 2);
  RgbaImage source = MatteSheet(2, 2);
  for (int index = 0; index < 4; ++index) PaintFrame(source, index, 2);
  // Keep frame zero inside the canvas but lift it out of the contact band.
  for (int y = 9; y < 11; ++y) {
    for (int x = 2; x < 6; ++x) PaintPixel(source, x, y, kMatte);
  }
  PaintRect(source, 3, 7, 3, 2, kBody);

  const absl::Status status = RunAnimationArtworkFeasibility(source, config).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(status.message().find("does not reach the contact line"), std::string::npos);
}

TEST(AnimationArtworkFeasibilityTest, RejectsDuplicateAdjacentFrames) {
  const AnimationArtworkFeasibilityConfig config =
      SyntheticConfig(AnimationFeasibilityClip::kIdleRight, 2, 2);
  RgbaImage source = MatteSheet(2, 2);
  for (int index = 0; index < 4; ++index) PaintFrame(source, index, 2);
  CopyFrame(source, 0, 1, 2);

  const absl::Status status = RunAnimationArtworkFeasibility(source, config).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(status.message().find("exact duplicates"), std::string::npos);
}

TEST(AnimationArtworkFeasibilityTest, RejectsDuplicateLoopClosure) {
  const AnimationArtworkFeasibilityConfig config =
      SyntheticConfig(AnimationFeasibilityClip::kIdleRight, 2, 2);
  RgbaImage source = MatteSheet(2, 2);
  for (int index = 0; index < 4; ++index) PaintFrame(source, index, 2);
  CopyFrame(source, 0, 3, 2);

  const absl::Status status = RunAnimationArtworkFeasibility(source, config).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(status.message().find("exact duplicates"), std::string::npos);
}

TEST(AnimationArtworkFeasibilityTest, RepeatingTheSameInputIsByteDeterministic) {
  const AnimationArtworkFeasibilityConfig config =
      SyntheticConfig(AnimationFeasibilityClip::kIdleRight, 2, 2);
  RgbaImage source = MatteSheet(2, 2);
  for (int index = 0; index < 4; ++index) PaintFrame(source, index, 2);

  ASSERT_OK_AND_ASSIGN(const AnimationArtworkFeasibilityResult first,
                       RunAnimationArtworkFeasibility(source, config));
  ASSERT_OK_AND_ASSIGN(const AnimationArtworkFeasibilityResult second,
                       RunAnimationArtworkFeasibility(source, config));

  EXPECT_EQ(first.source_digest, second.source_digest);
  EXPECT_EQ(first.packed_digest, second.packed_digest);
  ExpectSameImage(first.packed_texture, second.packed_texture);
  ASSERT_EQ(first.frames.size(), second.frames.size());
  ASSERT_EQ(first.differences.size(), second.differences.size());
  for (size_t index = 0; index < first.frames.size(); ++index) {
    ExpectSameImage(first.frames[index].extracted, second.frames[index].extracted);
    ExpectSameImage(first.frames[index].isolated, second.frames[index].isolated);
    ExpectSameImage(first.frames[index].resized, second.frames[index].resized);
    ExpectSameImage(first.frames[index].finished, second.frames[index].finished);
    ExpectSameDiagnostics(first.frames[index].diagnostics, second.frames[index].diagnostics);
    ExpectSameImage(first.differences[index].image, second.differences[index].image);
    EXPECT_EQ(first.differences[index].changed_pixels, second.differences[index].changed_pixels);
    EXPECT_EQ(first.differences[index].maximum_channel_difference,
              second.differences[index].maximum_channel_difference);
  }
  EXPECT_EQ(first.sprite_frames, second.sprite_frames);
}

}  // namespace
}  // namespace zebes
