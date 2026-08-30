#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "objects/sprite.h"
#include "terrain/terrain_palette.h"

namespace zebes {

enum class AnimationFeasibilityClip {
  kIdleRight = 0,
  kLocomotionRight = 1,
};

std::string_view AnimationFeasibilityClipId(AnimationFeasibilityClip clip);

struct AnimationFeasibilitySheetLayout {
  int grid_x = 0;
  int grid_y = 0;
  int cell_width = 0;
  int cell_height = 0;
  int column_gap = 0;
  int row_gap = 0;
  int columns = 0;
  int rows = 0;
};

// This is an experiment contract, not a serialized recipe. Every cell uses the
// same extraction, resize, matte, palette, and registration settings. Changing
// one frame independently is intentionally impossible at this boundary.
struct AnimationArtworkFeasibilityConfig {
  AnimationFeasibilityClip clip = AnimationFeasibilityClip::kIdleRight;
  AnimationFeasibilitySheetLayout sheet;
  int output_width = 48;
  int output_height = 48;
  int origin_x = 24;
  int origin_y = 44;
  int contact_line_y = 44;
  int render_scale = 2;
  int contact_tolerance = 2;
  int alpha_threshold = 128;
  int minimum_visible_pixels = 16;
  int maximum_source_width = 2048;
  int maximum_source_height = 2048;
  size_t maximum_source_pixels = 16 * 1024 * 1024;
  RgbaColor matte = {255, 0, 255, 255};
  float transparent_matte_distance = 24.0f;
  float opaque_matte_distance = 190.0f;
  std::vector<RgbaColor> palette;
  std::vector<int> frames_per_cycle;
  std::vector<bool> planted_frames;
};

struct AnimationFeasibilityBounds {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  bool operator==(const AnimationFeasibilityBounds& other) const = default;
};

struct AnimationFeasibilityFrameDiagnostics {
  int index = 0;
  AnimationFeasibilityBounds bounds;
  size_t visible_pixels = 0;
  bool contact_line_hit = false;
};

struct AnimationFeasibilityFrameDifference {
  int from_index = 0;
  int to_index = 0;
  size_t changed_pixels = 0;
  int maximum_channel_difference = 0;
  RgbaImage image;
};

struct AnimationFeasibilityFrameResult {
  RgbaImage extracted;
  RgbaImage isolated;
  RgbaImage resized;
  RgbaImage finished;
  AnimationFeasibilityFrameDiagnostics diagnostics;
};

struct AnimationArtworkFeasibilityResult {
  std::string source_digest;
  std::vector<AnimationFeasibilityFrameResult> frames;
  RgbaImage packed_texture;
  std::vector<SpriteFrame> sprite_frames;
  std::vector<AnimationFeasibilityFrameDifference> differences;
  std::string packed_digest;
};

absl::Status ValidateAnimationArtworkFeasibilityConfig(
    const AnimationArtworkFeasibilityConfig& config);

absl::StatusOr<AnimationArtworkFeasibilityConfig> MakeAnimationArtworkFeasibilityConfig(
    AnimationFeasibilityClip clip, AnimationFeasibilitySheetLayout sheet,
    std::vector<RgbaColor> palette);

// Builds an explicitly authored disposable contract. Unlike the legacy
// convenience preset above, this does not infer timing or planted-foot
// expectations from the clip name or grid shape.
absl::StatusOr<AnimationArtworkFeasibilityConfig> MakeAuthoredAnimationArtworkFeasibilityConfig(
    AnimationFeasibilityClip clip, AnimationFeasibilitySheetLayout sheet,
    std::vector<RgbaColor> palette, std::vector<int> frames_per_cycle,
    std::vector<bool> planted_frames);

absl::StatusOr<AnimationArtworkFeasibilityResult> RunAnimationArtworkFeasibility(
    const RgbaImage& source, const AnimationArtworkFeasibilityConfig& config);

}  // namespace zebes
