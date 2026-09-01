#include "artwork/animation_frame_set_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "artwork/generated_artwork_postprocessor.h"
#include "common/image_digest.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

constexpr size_t kMaximumFrames = 64;
constexpr size_t kMaximumPaletteColors = 64;
constexpr int kMaximumTextureDimension = 4096;

absl::Status ValidateExtraction(AnimationFrameSetExtraction extraction) {
  switch (extraction) {
    case AnimationFrameSetExtraction::kPreserveAlpha:
    case AnimationFrameSetExtraction::kRemoveSolidMatte:
      return absl::OkStatus();
  }
  return absl::InvalidArgumentError("animation frame-set extraction policy is invalid");
}

absl::Status ValidateSheetLayout(const AnimationFrameSetSheetLayout& sheet) {
  if (sheet.grid_x < 0 || sheet.grid_y < 0 || sheet.cell_width <= 0 || sheet.cell_height <= 0 ||
      sheet.column_gap < 0 || sheet.row_gap < 0 || sheet.columns <= 0 || sheet.rows <= 0) {
    return absl::InvalidArgumentError("animation frame-set sheet layout is invalid");
  }
  const int64_t frame_count = static_cast<int64_t>(sheet.columns) * sheet.rows;
  if (frame_count <= 0 || frame_count > static_cast<int64_t>(kMaximumFrames)) {
    return absl::InvalidArgumentError("animation frame-set frame count is out of range");
  }
  return absl::OkStatus();
}

absl::Status ValidatePalette(const AnimationFrameSetStyle& style) {
  if (style.palette.empty() || style.palette.size() > kMaximumPaletteColors) {
    return absl::InvalidArgumentError(
        "animation frame-set palette must contain between 1 and 64 colors");
  }
  for (size_t index = 0; index < style.palette.size(); ++index) {
    if (style.palette[index].a != 255) {
      return absl::InvalidArgumentError("animation frame-set palette colors must be opaque");
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (style.palette[prior] == style.palette[index]) {
        return absl::InvalidArgumentError("animation frame-set palette colors must be unique");
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateFrameVectors(const AnimationFrameSetPipelineConfig& config) {
  const size_t frame_count = static_cast<size_t>(config.sheet.columns) * config.sheet.rows;
  if (config.frames_per_cycle.size() != frame_count ||
      config.planted_frames.size() != frame_count) {
    return absl::InvalidArgumentError(
        "animation frame-set timing and contact vectors must match the sheet frame count");
  }
  if (std::any_of(config.frames_per_cycle.begin(), config.frames_per_cycle.end(),
                  [](int ticks) { return ticks <= 0; })) {
    return absl::InvalidArgumentError("animation frame-set frame timing must be positive");
  }
  return absl::OkStatus();
}

absl::Status ValidatePackedGeometry(const AnimationFrameSetPipelineConfig& config) {
  const int frame_count = config.sheet.columns * config.sheet.rows;
  if (config.packing_columns <= 0 || config.packing_columns > frame_count ||
      frame_count % config.packing_columns != 0) {
    return absl::InvalidArgumentError(
        "animation frame-set packing columns must divide the frame count");
  }
  const int64_t packed_width = static_cast<int64_t>(config.output_width) * config.packing_columns;
  const int64_t packed_height =
      static_cast<int64_t>(config.output_height) * (frame_count / config.packing_columns);
  if (packed_width > kMaximumTextureDimension || packed_height > kMaximumTextureDimension) {
    return absl::InvalidArgumentError("animation frame-set packed texture exceeds limits");
  }
  return absl::OkStatus();
}

int64_t GridRight(const AnimationFrameSetSheetLayout& sheet) {
  return static_cast<int64_t>(sheet.grid_x) +
         static_cast<int64_t>(sheet.columns) * sheet.cell_width +
         static_cast<int64_t>(sheet.columns - 1) * sheet.column_gap;
}

int64_t GridBottom(const AnimationFrameSetSheetLayout& sheet) {
  return static_cast<int64_t>(sheet.grid_y) + static_cast<int64_t>(sheet.rows) * sheet.cell_height +
         static_cast<int64_t>(sheet.rows - 1) * sheet.row_gap;
}

double ColorDistanceSquared(RgbaColor left, RgbaColor right) {
  const double red = static_cast<double>(left.r) - right.r;
  const double green = static_cast<double>(left.g) - right.g;
  const double blue = static_cast<double>(left.b) - right.b;
  return red * red + green * green + blue * blue;
}

bool IsDeclaredCellPixel(int x, int y, const AnimationFrameSetSheetLayout& sheet) {
  if (x < sheet.grid_x || y < sheet.grid_y || x >= GridRight(sheet) || y >= GridBottom(sheet)) {
    return false;
  }
  const int cell_stride_x = sheet.cell_width + sheet.column_gap;
  const int cell_stride_y = sheet.cell_height + sheet.row_gap;
  const int local_x = x - sheet.grid_x;
  const int local_y = y - sheet.grid_y;
  return local_x / cell_stride_x < sheet.columns && local_y / cell_stride_y < sheet.rows &&
         local_x % cell_stride_x < sheet.cell_width && local_y % cell_stride_y < sheet.cell_height;
}

bool IsBackgroundPixel(RgbaColor color, const AnimationFrameSetStyle& style) {
  if (color.a < style.alpha_threshold) return true;
  if (style.extraction == AnimationFrameSetExtraction::kPreserveAlpha) return false;
  const double transparent_squared =
      static_cast<double>(style.transparent_matte_distance) * style.transparent_matte_distance;
  return ColorDistanceSquared(color, style.matte) <= transparent_squared;
}

absl::Status ValidateSourceGeometry(const RgbaImage& source,
                                    const AnimationFrameSetPipelineConfig& config,
                                    const AnimationFrameSetStyle& style) {
  RETURN_IF_ERROR(ValidateSourceArtworkPixels(source, config.source_limits));
  if (GridRight(config.sheet) > source.width || GridBottom(config.sheet) > source.height) {
    return absl::InvalidArgumentError(
        "animation frame-set sheet cells extend beyond the source image");
  }
  for (int y = 0; y < source.height; ++y) {
    for (int x = 0; x < source.width; ++x) {
      if (IsDeclaredCellPixel(x, y, config.sheet)) continue;
      const size_t offset = (static_cast<size_t>(y) * source.width + x) * 4;
      const RgbaColor color{
          .r = source.pixels[offset + 0],
          .g = source.pixels[offset + 1],
          .b = source.pixels[offset + 2],
          .a = source.pixels[offset + 3],
      };
      if (!IsBackgroundPixel(color, style)) {
        return absl::InvalidArgumentError(absl::StrCat(
            "animation frame-set has foreground outside declared cells at ", x, ",", y));
      }
    }
  }
  return absl::OkStatus();
}

RgbaImage ExtractCell(const RgbaImage& source, const AnimationFrameSetSheetLayout& sheet,
                      int index) {
  const int column = index % sheet.columns;
  const int row = index / sheet.columns;
  const int source_x = sheet.grid_x + column * (sheet.cell_width + sheet.column_gap);
  const int source_y = sheet.grid_y + row * (sheet.cell_height + sheet.row_gap);
  RgbaImage cell{
      .width = sheet.cell_width,
      .height = sheet.cell_height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(sheet.cell_width) * sheet.cell_height * 4),
  };
  for (int y = 0; y < sheet.cell_height; ++y) {
    const size_t source_offset = (static_cast<size_t>(source_y + y) * source.width + source_x) * 4;
    const size_t destination_offset = static_cast<size_t>(y) * sheet.cell_width * 4;
    std::copy_n(source.pixels.begin() + static_cast<ptrdiff_t>(source_offset),
                static_cast<size_t>(sheet.cell_width) * 4,
                cell.pixels.begin() + static_cast<ptrdiff_t>(destination_offset));
  }
  return cell;
}

GeneratedArtworkPostprocessConfig PostprocessConfig(const AnimationFrameSetStyle& style,
                                                    const AnimationFrameSetPipelineConfig& config) {
  return {
      .output_width = config.output_width,
      .output_height = config.output_height,
      .pixel_block_size = 1,
      .background_policy = style.extraction == AnimationFrameSetExtraction::kPreserveAlpha
                               ? GeneratedArtworkBackgroundPolicy::kPreserve
                               : GeneratedArtworkBackgroundPolicy::kRemoveSolidMatte,
      .palette_policy = GeneratedArtworkPalettePolicy::kQuantize,
      .alpha_policy = GeneratedArtworkAlphaPolicy::kBinary,
      .background = style.matte,
      .transparent_distance = style.transparent_matte_distance,
      .opaque_distance = style.opaque_matte_distance,
      .final_alpha_threshold = style.alpha_threshold,
      .palette_alpha_threshold = style.alpha_threshold,
      .maximum_palette_colors = static_cast<int>(kMaximumPaletteColors),
      .minimum_visible_pixels = config.minimum_visible_pixels,
      .minimum_transparent_border = 1,
  };
}

bool HasPixelsAtOrBelowContact(const RgbaImage& frame, int contact_line_y) {
  for (int y = contact_line_y; y < frame.height; ++y) {
    for (int x = 0; x < frame.width; ++x) {
      const size_t alpha = (static_cast<size_t>(y) * frame.width + x) * 4 + 3;
      if (frame.pixels[alpha] != 0) return true;
    }
  }
  return false;
}

bool HasContact(const RgbaImage& frame, int contact_line_y, int tolerance) {
  const int first_y = std::max(0, contact_line_y - tolerance);
  for (int y = first_y; y < contact_line_y; ++y) {
    for (int x = 0; x < frame.width; ++x) {
      const size_t alpha = (static_cast<size_t>(y) * frame.width + x) * 4 + 3;
      if (frame.pixels[alpha] != 0) return true;
    }
  }
  return false;
}

absl::StatusOr<AnimationFrameSetFrameResult> ProcessFrame(
    RgbaImage extracted, int index, const AnimationFrameSetStyle& style,
    const AnimationFrameSetPipelineConfig& config) {
  ASSIGN_OR_RETURN(
      GeneratedArtworkPostprocessResult processed,
      PostprocessGeneratedArtwork(extracted, style.palette, PostprocessConfig(style, config)));
  if (HasPixelsAtOrBelowContact(processed.finished, config.contact_line_y)) {
    return absl::FailedPreconditionError(absl::StrCat("animation frame-set frame ", index,
                                                      " has foreground below the contact line"));
  }
  const bool contact_line_hit =
      HasContact(processed.finished, config.contact_line_y, config.contact_tolerance);
  if (config.planted_frames[static_cast<size_t>(index)] && !contact_line_hit) {
    return absl::FailedPreconditionError(absl::StrCat("animation frame-set planted frame ", index,
                                                      " does not reach the contact line"));
  }

  const GeneratedArtworkBounds bounds = processed.diagnostics.visible_bounds;
  const int horizontal_anchor_drift =
      bounds.left + (bounds.right - bounds.left) / 2 - config.origin_x;
  const int vertical_anchor_drift = config.contact_line_y - bounds.bottom;
  if (std::abs(horizontal_anchor_drift) > config.maximum_horizontal_anchor_drift ||
      std::abs(vertical_anchor_drift) > config.maximum_vertical_anchor_drift) {
    return absl::FailedPreconditionError(
        absl::StrCat("animation frame-set frame ", index, " exceeds authored anchor drift"));
  }

  return AnimationFrameSetFrameResult{
      .extracted = std::move(extracted),
      .isolated = std::move(processed.matted),
      .rasterized = std::move(processed.resized),
      .finished = std::move(processed.finished),
      .diagnostics =
          {
              .index = index,
              .bounds =
                  {
                      .left = bounds.left,
                      .top = bounds.top,
                      .right = bounds.right,
                      .bottom = bounds.bottom,
                  },
              .visible_pixels = processed.diagnostics.visible_pixels,
              .contact_line_hit = contact_line_hit,
              .horizontal_anchor_drift = horizontal_anchor_drift,
              .vertical_anchor_drift = vertical_anchor_drift,
          },
  };
}

RgbaImage PackFrames(const std::vector<AnimationFrameSetFrameResult>& frames,
                     const AnimationFrameSetPipelineConfig& config) {
  const int packing_rows = static_cast<int>(frames.size()) / config.packing_columns;
  RgbaImage packed{
      .width = config.output_width * config.packing_columns,
      .height = config.output_height * packing_rows,
      .pixels =
          std::vector<uint8_t>(static_cast<size_t>(config.output_width) * config.packing_columns *
                               config.output_height * packing_rows * 4),
  };
  for (size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
    const int destination_x =
        static_cast<int>(frame_index % static_cast<size_t>(config.packing_columns)) *
        config.output_width;
    const int destination_y =
        static_cast<int>(frame_index / static_cast<size_t>(config.packing_columns)) *
        config.output_height;
    const RgbaImage& frame = frames[frame_index].finished;
    for (int y = 0; y < config.output_height; ++y) {
      const size_t source_offset = static_cast<size_t>(y) * frame.width * 4;
      const size_t destination_offset =
          (static_cast<size_t>(destination_y + y) * packed.width + destination_x) * 4;
      std::copy_n(frame.pixels.begin() + static_cast<ptrdiff_t>(source_offset),
                  static_cast<size_t>(config.output_width) * 4,
                  packed.pixels.begin() + static_cast<ptrdiff_t>(destination_offset));
    }
  }
  return packed;
}

std::vector<SpriteFrame> BuildSpriteFrames(const AnimationFrameSetPipelineConfig& config) {
  std::vector<SpriteFrame> frames;
  frames.reserve(config.frames_per_cycle.size());
  for (size_t index = 0; index < config.frames_per_cycle.size(); ++index) {
    frames.push_back({
        .index = static_cast<int>(index),
        .texture_x = static_cast<int>(index % static_cast<size_t>(config.packing_columns)) *
                     config.output_width,
        .texture_y = static_cast<int>(index / static_cast<size_t>(config.packing_columns)) *
                     config.output_height,
        .texture_w = config.output_width,
        .texture_h = config.output_height,
        .render_w = config.output_width * config.render_scale,
        .render_h = config.output_height * config.render_scale,
        .frames_per_cycle = config.frames_per_cycle[index],
        .offset_x = -config.origin_x * config.render_scale,
        .offset_y = -config.origin_y * config.render_scale,
    });
  }
  return frames;
}

}  // namespace

absl::Status ValidateAnimationFrameSetStyle(const AnimationFrameSetStyle& style) {
  RETURN_IF_ERROR(ValidateExtraction(style.extraction));
  constexpr double kMaximumRgbDistanceSquared = 3.0 * 255.0 * 255.0;
  if (style.matte.a != 255 || !std::isfinite(style.transparent_matte_distance) ||
      !std::isfinite(style.opaque_matte_distance) || style.transparent_matte_distance < 0.0f ||
      style.opaque_matte_distance <= style.transparent_matte_distance ||
      static_cast<double>(style.opaque_matte_distance) * style.opaque_matte_distance >
          kMaximumRgbDistanceSquared) {
    return absl::InvalidArgumentError("animation frame-set matte settings are invalid");
  }
  if (style.alpha_threshold < 1 || style.alpha_threshold > 255) {
    return absl::InvalidArgumentError("animation frame-set alpha threshold is invalid");
  }
  return ValidatePalette(style);
}

absl::Status ValidateAnimationFrameSetPipelineConfig(const AnimationFrameSetPipelineConfig& config,
                                                     const AnimationFrameSetStyle& style) {
  RETURN_IF_ERROR(ValidateAnimationFrameSetStyle(style));
  RETURN_IF_ERROR(ValidateSourceArtworkLimits(config.source_limits));
  RETURN_IF_ERROR(ValidateSheetLayout(config.sheet));
  if (config.output_width <= 0 || config.output_height <= 0 ||
      config.output_width > kMaximumTextureDimension ||
      config.output_height > kMaximumTextureDimension) {
    return absl::InvalidArgumentError("animation frame-set output dimensions are invalid");
  }
  if (static_cast<int64_t>(config.sheet.cell_width) * config.output_height !=
      static_cast<int64_t>(config.sheet.cell_height) * config.output_width) {
    return absl::InvalidArgumentError(
        "animation frame-set source cells and output canvas must use one uniform scale");
  }
  if (config.origin_x < 0 || config.origin_x > config.output_width || config.origin_y < 0 ||
      config.origin_y > config.output_height || config.contact_line_y <= 0 ||
      config.contact_line_y > config.output_height || config.origin_y != config.contact_line_y) {
    return absl::InvalidArgumentError(
        "animation frame-set origin and contact line are inconsistent with the canvas");
  }
  if (config.render_scale <= 0 ||
      static_cast<int64_t>(config.output_width) * config.render_scale >
          std::numeric_limits<int>::max() ||
      static_cast<int64_t>(config.output_height) * config.render_scale >
          std::numeric_limits<int>::max() ||
      config.contact_tolerance <= 0 || config.contact_tolerance > config.contact_line_y ||
      config.minimum_visible_pixels <= 0 || config.maximum_horizontal_anchor_drift < 0 ||
      config.maximum_vertical_anchor_drift < 0) {
    return absl::InvalidArgumentError(
        "animation frame-set processing and registration settings are invalid");
  }
  if (!IsValidSpritePlaybackMode(config.playback_mode)) {
    return absl::InvalidArgumentError("animation frame-set playback mode is invalid");
  }
  RETURN_IF_ERROR(ValidatePackedGeometry(config));
  return ValidateFrameVectors(config);
}

absl::StatusOr<AnimationFrameSetPipelineResult> RunAnimationFrameSetPipeline(
    const RgbaImage& source, const AnimationFrameSetStyle& style,
    const AnimationFrameSetPipelineConfig& config) {
  RETURN_IF_ERROR(ValidateAnimationFrameSetPipelineConfig(config, style));
  RETURN_IF_ERROR(ValidateSourceGeometry(source, config, style));
  ASSIGN_OR_RETURN(const std::string source_digest, RgbaImageDigest(source));

  const int frame_count = config.sheet.columns * config.sheet.rows;
  std::vector<AnimationFrameSetFrameResult> frames;
  frames.reserve(static_cast<size_t>(frame_count));
  for (int index = 0; index < frame_count; ++index) {
    RgbaImage extracted = ExtractCell(source, config.sheet, index);
    ASSIGN_OR_RETURN(AnimationFrameSetFrameResult frame,
                     ProcessFrame(std::move(extracted), index, style, config));
    frames.push_back(std::move(frame));
  }

  RgbaImage packed_texture = PackFrames(frames, config);
  ASSIGN_OR_RETURN(const std::string packed_digest, RgbaImageDigest(packed_texture));
  return AnimationFrameSetPipelineResult{
      .source_digest = source_digest,
      .packed_digest = packed_digest,
      .playback_mode = config.playback_mode,
      .frames = std::move(frames),
      .packed_texture = std::move(packed_texture),
      .sprite_frames = BuildSpriteFrames(config),
  };
}

}  // namespace zebes
