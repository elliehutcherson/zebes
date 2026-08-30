#include "artwork/animation_artwork_feasibility.h"

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
constexpr size_t kMaximumPaletteColors = 12;

absl::Status ValidateClip(AnimationFeasibilityClip clip) {
  if (AnimationFeasibilityClipId(clip).empty()) {
    return absl::InvalidArgumentError("animation feasibility clip is invalid");
  }
  return absl::OkStatus();
}

absl::Status ValidateSheetLayout(const AnimationFeasibilitySheetLayout& sheet) {
  if (sheet.grid_x < 0 || sheet.grid_y < 0 || sheet.cell_width <= 0 || sheet.cell_height <= 0 ||
      sheet.column_gap < 0 || sheet.row_gap < 0 || sheet.columns <= 0 || sheet.rows <= 0) {
    return absl::InvalidArgumentError("animation feasibility sheet layout is invalid");
  }
  if (sheet.cell_width != sheet.cell_height) {
    return absl::InvalidArgumentError(
        "animation feasibility cells must be square so the shared resize cannot distort poses");
  }
  const int64_t frame_count = static_cast<int64_t>(sheet.columns) * sheet.rows;
  if (frame_count <= 0 || frame_count > static_cast<int64_t>(kMaximumFrames)) {
    return absl::InvalidArgumentError("animation feasibility frame count is out of range");
  }
  return absl::OkStatus();
}

absl::Status ValidatePalette(const AnimationArtworkFeasibilityConfig& config) {
  if (config.palette.empty() || config.palette.size() > kMaximumPaletteColors) {
    return absl::InvalidArgumentError(
        "animation feasibility palette must contain between 1 and 12 colors");
  }
  for (size_t index = 0; index < config.palette.size(); ++index) {
    if (config.palette[index].a != 255) {
      return absl::InvalidArgumentError("animation feasibility palette colors must be opaque");
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (config.palette[prior] == config.palette[index]) {
        return absl::InvalidArgumentError("animation feasibility palette colors must be unique");
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateFrameVectors(const AnimationArtworkFeasibilityConfig& config) {
  const size_t frame_count = static_cast<size_t>(config.sheet.columns) * config.sheet.rows;
  if (config.frames_per_cycle.size() != frame_count ||
      config.planted_frames.size() != frame_count) {
    return absl::InvalidArgumentError(
        "animation feasibility timing and contact vectors must match the sheet frame count");
  }
  if (std::ranges::any_of(config.frames_per_cycle, [](int ticks) { return ticks <= 0; })) {
    return absl::InvalidArgumentError("animation feasibility frame timing must be positive");
  }
  return absl::OkStatus();
}

absl::Status ValidateSourceBounds(const RgbaImage& source,
                                  const AnimationArtworkFeasibilityConfig& config) {
  if (!source.IsValid()) {
    return absl::InvalidArgumentError("animation feasibility source image is invalid");
  }
  if (source.width > config.maximum_source_width || source.height > config.maximum_source_height) {
    return absl::ResourceExhaustedError("animation feasibility source dimensions exceed limits");
  }
  const int64_t source_pixels = static_cast<int64_t>(source.width) * source.height;
  if (source_pixels > static_cast<int64_t>(config.maximum_source_pixels)) {
    return absl::ResourceExhaustedError("animation feasibility source pixel count exceeds limit");
  }

  const AnimationFeasibilitySheetLayout& sheet = config.sheet;
  const int64_t grid_right = static_cast<int64_t>(sheet.grid_x) +
                             static_cast<int64_t>(sheet.columns) * sheet.cell_width +
                             static_cast<int64_t>(sheet.columns - 1) * sheet.column_gap;
  const int64_t grid_bottom = static_cast<int64_t>(sheet.grid_y) +
                              static_cast<int64_t>(sheet.rows) * sheet.cell_height +
                              static_cast<int64_t>(sheet.rows - 1) * sheet.row_gap;
  if (grid_right > source.width || grid_bottom > source.height) {
    return absl::InvalidArgumentError(
        "animation feasibility sheet cells extend beyond the source image");
  }
  return absl::OkStatus();
}

RgbaImage ExtractCell(const RgbaImage& source, const AnimationFeasibilitySheetLayout& sheet,
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

GeneratedArtworkPostprocessConfig PostprocessConfig(
    const AnimationArtworkFeasibilityConfig& config) {
  return {
      .output_width = config.output_width,
      .output_height = config.output_height,
      .pixel_block_size = 1,
      .background_policy = GeneratedArtworkBackgroundPolicy::kRemoveSolidMatte,
      .palette_policy = GeneratedArtworkPalettePolicy::kQuantize,
      .alpha_policy = GeneratedArtworkAlphaPolicy::kBinary,
      .background = config.matte,
      .transparent_distance = config.transparent_matte_distance,
      .opaque_distance = config.opaque_matte_distance,
      .final_alpha_threshold = config.alpha_threshold,
      .palette_alpha_threshold = config.alpha_threshold,
      .maximum_palette_colors = static_cast<int>(kMaximumPaletteColors),
      .minimum_visible_pixels = config.minimum_visible_pixels,
      .minimum_transparent_border = 1,
  };
}

bool HasContact(const RgbaImage& frame, int contact_line_y, int tolerance) {
  const int first_y = std::max(0, contact_line_y - tolerance);
  const int last_y = std::min(frame.height, contact_line_y);
  for (int y = first_y; y < last_y; ++y) {
    for (int x = 0; x < frame.width; ++x) {
      const size_t alpha = (static_cast<size_t>(y) * frame.width + x) * 4 + 3;
      if (frame.pixels[alpha] != 0) return true;
    }
  }
  return false;
}

bool HasPixelsBelowContact(const RgbaImage& frame, int contact_line_y) {
  for (int y = contact_line_y; y < frame.height; ++y) {
    for (int x = 0; x < frame.width; ++x) {
      const size_t alpha = (static_cast<size_t>(y) * frame.width + x) * 4 + 3;
      if (frame.pixels[alpha] != 0) return true;
    }
  }
  return false;
}

absl::StatusOr<AnimationFeasibilityFrameResult> ProcessFrame(
    const RgbaImage& extracted, int index, const AnimationArtworkFeasibilityConfig& config) {
  ASSIGN_OR_RETURN(
      const GeneratedArtworkPostprocessResult processed,
      PostprocessGeneratedArtwork(extracted, config.palette, PostprocessConfig(config)));
  if (HasPixelsBelowContact(processed.finished, config.contact_line_y)) {
    return absl::FailedPreconditionError(absl::StrCat("animation feasibility frame ", index,
                                                      " has foreground below the contact line"));
  }
  const bool contact_line_hit =
      HasContact(processed.finished, config.contact_line_y, config.contact_tolerance);
  if (config.planted_frames[static_cast<size_t>(index)] && !contact_line_hit) {
    return absl::FailedPreconditionError(absl::StrCat("animation feasibility planted frame ", index,
                                                      " does not reach the contact line"));
  }
  return AnimationFeasibilityFrameResult{
      .extracted = extracted,
      .isolated = processed.matted,
      .resized = processed.resized,
      .finished = processed.finished,
      .diagnostics =
          {
              .index = index,
              .bounds =
                  {
                      .left = processed.diagnostics.visible_bounds.left,
                      .top = processed.diagnostics.visible_bounds.top,
                      .right = processed.diagnostics.visible_bounds.right,
                      .bottom = processed.diagnostics.visible_bounds.bottom,
                  },
              .visible_pixels = processed.diagnostics.visible_pixels,
              .contact_line_hit = contact_line_hit,
          },
  };
}

absl::StatusOr<RgbaImage> PackFrames(const std::vector<AnimationFeasibilityFrameResult>& frames,
                                     const AnimationArtworkFeasibilityConfig& config) {
  const int64_t width = static_cast<int64_t>(config.output_width) * frames.size();
  if (!std::in_range<int>(width)) {
    return absl::OutOfRangeError("animation feasibility packed texture is too wide");
  }
  RgbaImage packed{
      .width = static_cast<int>(width),
      .height = config.contact_line_y,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(width) * config.contact_line_y * 4),
  };
  for (size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
    const RgbaImage& frame = frames[frame_index].finished;
    for (int y = 0; y < config.contact_line_y; ++y) {
      const size_t source_offset = static_cast<size_t>(y) * frame.width * 4;
      const size_t destination_offset =
          (static_cast<size_t>(y) * packed.width + frame_index * config.output_width) * 4;
      std::copy_n(frame.pixels.begin() + static_cast<ptrdiff_t>(source_offset),
                  static_cast<size_t>(config.output_width) * 4,
                  packed.pixels.begin() + static_cast<ptrdiff_t>(destination_offset));
    }
  }
  return packed;
}

std::vector<SpriteFrame> BuildSpriteFrames(const AnimationArtworkFeasibilityConfig& config) {
  std::vector<SpriteFrame> frames;
  frames.reserve(config.frames_per_cycle.size());
  for (size_t index = 0; index < config.frames_per_cycle.size(); ++index) {
    frames.push_back({
        .index = static_cast<int>(index),
        .texture_x = static_cast<int>(index) * config.output_width,
        .texture_y = 0,
        .texture_w = config.output_width,
        .texture_h = config.contact_line_y,
        .render_w = config.output_width * config.render_scale,
        .render_h = config.contact_line_y * config.render_scale,
        .frames_per_cycle = config.frames_per_cycle[index],
        .offset_x = -config.origin_x * config.render_scale,
        .offset_y = -config.origin_y * config.render_scale,
    });
  }
  return frames;
}

AnimationFeasibilityFrameDifference CompareFrames(const RgbaImage& from, const RgbaImage& to,
                                                  int from_index, int to_index) {
  RgbaImage difference{
      .width = from.width,
      .height = from.height,
      .pixels = std::vector<uint8_t>(from.pixels.size(), 255),
  };
  size_t changed_pixels = 0;
  int maximum_channel_difference = 0;
  const size_t pixel_count = static_cast<size_t>(from.width) * from.height;
  for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
    const size_t offset = pixel * 4;
    int pixel_difference = 0;
    for (size_t channel = 0; channel < 4; ++channel) {
      const int channel_difference = std::abs(static_cast<int>(from.pixels[offset + channel]) -
                                              static_cast<int>(to.pixels[offset + channel]));
      pixel_difference = std::max(pixel_difference, channel_difference);
      maximum_channel_difference = std::max(maximum_channel_difference, channel_difference);
    }
    difference.pixels[offset + 0] = static_cast<uint8_t>(pixel_difference);
    difference.pixels[offset + 1] = static_cast<uint8_t>(pixel_difference);
    difference.pixels[offset + 2] = static_cast<uint8_t>(pixel_difference);
    difference.pixels[offset + 3] = 255;
    if (pixel_difference != 0) ++changed_pixels;
  }
  return {
      .from_index = from_index,
      .to_index = to_index,
      .changed_pixels = changed_pixels,
      .maximum_channel_difference = maximum_channel_difference,
      .image = std::move(difference),
  };
}

absl::StatusOr<std::vector<AnimationFeasibilityFrameDifference>> BuildDifferences(
    const std::vector<AnimationFeasibilityFrameResult>& frames) {
  std::vector<AnimationFeasibilityFrameDifference> differences;
  differences.reserve(frames.size());
  for (size_t index = 0; index < frames.size(); ++index) {
    const size_t next = (index + 1) % frames.size();
    AnimationFeasibilityFrameDifference difference =
        CompareFrames(frames[index].finished, frames[next].finished, static_cast<int>(index),
                      static_cast<int>(next));
    if (difference.changed_pixels == 0) {
      return absl::FailedPreconditionError(absl::StrCat("animation feasibility frames ", index,
                                                        " and ", next, " are exact duplicates"));
    }
    differences.push_back(std::move(difference));
  }
  return differences;
}

}  // namespace

std::string_view AnimationFeasibilityClipId(AnimationFeasibilityClip clip) {
  switch (clip) {
    case AnimationFeasibilityClip::kIdleRight:
      return "idle-right";
    case AnimationFeasibilityClip::kLocomotionRight:
      return "locomotion-right";
  }
  return {};
}

absl::Status ValidateAnimationArtworkFeasibilityConfig(
    const AnimationArtworkFeasibilityConfig& config) {
  RETURN_IF_ERROR(ValidateClip(config.clip));
  RETURN_IF_ERROR(ValidateSheetLayout(config.sheet));
  if (config.output_width <= 0 || config.output_height <= 0 || config.output_width > 4096 ||
      config.output_height > 4096) {
    return absl::InvalidArgumentError("animation feasibility output dimensions are invalid");
  }
  if (config.origin_x < 0 || config.origin_x > config.output_width || config.origin_y < 0 ||
      config.origin_y > config.output_height || config.contact_line_y <= 0 ||
      config.contact_line_y > config.output_height || config.origin_y != config.contact_line_y) {
    return absl::InvalidArgumentError(
        "animation feasibility origin and contact line are inconsistent with the canvas");
  }
  if (config.render_scale <= 0 || config.contact_tolerance <= 0 ||
      config.contact_tolerance > config.contact_line_y || config.alpha_threshold < 1 ||
      config.alpha_threshold > 255 || config.minimum_visible_pixels <= 0) {
    return absl::InvalidArgumentError("animation feasibility processing settings are invalid");
  }
  if (config.maximum_source_width <= 0 || config.maximum_source_height <= 0 ||
      config.maximum_source_pixels == 0) {
    return absl::InvalidArgumentError("animation feasibility source limits must be positive");
  }
  if (config.matte.a != 255 || !std::isfinite(config.transparent_matte_distance) ||
      !std::isfinite(config.opaque_matte_distance) || config.transparent_matte_distance < 0.0f ||
      config.opaque_matte_distance <= config.transparent_matte_distance) {
    return absl::InvalidArgumentError("animation feasibility matte settings are invalid");
  }
  RETURN_IF_ERROR(ValidatePalette(config));
  return ValidateFrameVectors(config);
}

absl::StatusOr<AnimationArtworkFeasibilityConfig> MakeAnimationArtworkFeasibilityConfig(
    AnimationFeasibilityClip clip, AnimationFeasibilitySheetLayout sheet,
    std::vector<RgbaColor> palette) {
  AnimationArtworkFeasibilityConfig config{
      .clip = clip,
      .sheet = sheet,
      .palette = std::move(palette),
  };
  switch (clip) {
    case AnimationFeasibilityClip::kIdleRight:
      if (sheet.columns != 2 || sheet.rows != 2) {
        return absl::InvalidArgumentError("idle feasibility sheet must use a 2x2 grid");
      }
      config.frames_per_cycle.assign(4, 15);
      config.planted_frames.assign(4, true);
      break;
    case AnimationFeasibilityClip::kLocomotionRight:
      if (sheet.columns != 5 || sheet.rows != 2) {
        return absl::InvalidArgumentError("locomotion feasibility sheet must use a 5x2 grid");
      }
      config.frames_per_cycle.assign(10, 4);
      config.planted_frames = {true, true, false, false, false, true, true, false, false, false};
      break;
    default:
      return absl::InvalidArgumentError("animation feasibility clip is invalid");
  }
  RETURN_IF_ERROR(ValidateAnimationArtworkFeasibilityConfig(config));
  return config;
}

absl::StatusOr<AnimationArtworkFeasibilityConfig> MakeAuthoredAnimationArtworkFeasibilityConfig(
    AnimationFeasibilityClip clip, AnimationFeasibilitySheetLayout sheet,
    std::vector<RgbaColor> palette, std::vector<int> frames_per_cycle,
    std::vector<bool> planted_frames) {
  AnimationArtworkFeasibilityConfig config{
      .clip = clip,
      .sheet = sheet,
      .palette = std::move(palette),
      .frames_per_cycle = std::move(frames_per_cycle),
      .planted_frames = std::move(planted_frames),
  };
  RETURN_IF_ERROR(ValidateAnimationArtworkFeasibilityConfig(config));
  return config;
}

absl::StatusOr<AnimationArtworkFeasibilityResult> RunAnimationArtworkFeasibility(
    const RgbaImage& source, const AnimationArtworkFeasibilityConfig& config) {
  RETURN_IF_ERROR(ValidateAnimationArtworkFeasibilityConfig(config));
  RETURN_IF_ERROR(ValidateSourceBounds(source, config));
  ASSIGN_OR_RETURN(const std::string source_digest, RgbaImageDigest(source));

  const int frame_count = config.sheet.columns * config.sheet.rows;
  std::vector<AnimationFeasibilityFrameResult> frames;
  frames.reserve(static_cast<size_t>(frame_count));
  for (int index = 0; index < frame_count; ++index) {
    RgbaImage extracted = ExtractCell(source, config.sheet, index);
    ASSIGN_OR_RETURN(AnimationFeasibilityFrameResult frame, ProcessFrame(extracted, index, config));
    frames.push_back(std::move(frame));
  }
  ASSIGN_OR_RETURN(RgbaImage packed_texture, PackFrames(frames, config));
  ASSIGN_OR_RETURN(const std::string packed_digest, RgbaImageDigest(packed_texture));
  ASSIGN_OR_RETURN(std::vector<AnimationFeasibilityFrameDifference> differences,
                   BuildDifferences(frames));
  return AnimationArtworkFeasibilityResult{
      .source_digest = source_digest,
      .frames = std::move(frames),
      .packed_texture = std::move(packed_texture),
      .sprite_frames = BuildSpriteFrames(config),
      .differences = std::move(differences),
      .packed_digest = packed_digest,
  };
}

}  // namespace zebes
