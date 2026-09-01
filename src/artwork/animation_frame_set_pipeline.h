#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "objects/sprite.h"
#include "terrain/terrain_palette.h"

namespace zebes {

inline constexpr int kAnimationFrameSetPipelineVersion = 1;

enum class AnimationFrameSetExtraction : uint8_t {
  // Retains authored alpha. Transparent source pixels may contain arbitrary RGB;
  // the finished frames clear those channels deterministically.
  kPreserveAlpha = 0,
  // Removes only the configured solid matte and its connected transition edge.
  kRemoveSolidMatte = 1,
};

// Defines an ordered row-major set of equal-sized source cells. Pixels outside
// these cells, including declared gaps and margins, must be background.
struct AnimationFrameSetSheetLayout {
  int grid_x = 0;
  int grid_y = 0;
  int cell_width = 0;
  int cell_height = 0;
  int column_gap = 0;
  int row_gap = 0;
  int columns = 0;
  int rows = 0;
};

// Processing choices resolved by the authoring layer. One palette and one
// extraction policy apply to every frame; per-frame treatment is unsupported.
struct AnimationFrameSetStyle {
  AnimationFrameSetExtraction extraction = AnimationFrameSetExtraction::kPreserveAlpha;
  RgbaColor matte = {255, 0, 255, 255};
  float transparent_matte_distance = 24.0f;
  float opaque_matte_distance = 190.0f;
  int alpha_threshold = 128;
  std::vector<RgbaColor> palette;
};

// Pure, source-neutral processing contract. Source cells are resized as whole
// canvases at one uniform scale, preserving their common authored registration.
// The origin and contact line describe output-frame coordinates only; they do
// not alter collision geometry.
struct AnimationFrameSetPipelineConfig {
  SourceArtworkLimits source_limits;
  AnimationFrameSetSheetLayout sheet;
  int output_width = 48;
  int output_height = 48;
  int origin_x = 24;
  int origin_y = 44;
  int contact_line_y = 44;
  int render_scale = 2;
  int contact_tolerance = 2;
  int minimum_visible_pixels = 16;
  int maximum_horizontal_anchor_drift = 24;
  int maximum_vertical_anchor_drift = 44;
  // Packed texture columns. The frame count must divide evenly so no implicit
  // or unused atlas cells exist. Set this to the frame count for a strip.
  int packing_columns = 1;
  SpritePlaybackMode playback_mode = SpritePlaybackMode::kLoop;
  std::vector<int> frames_per_cycle;
  // A planted frame must visibly reach the contact band. Airborne or otherwise
  // unplanted frames remain registered to the same origin without this check.
  std::vector<bool> planted_frames;
};

struct AnimationFrameSetBounds {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  bool operator==(const AnimationFrameSetBounds& other) const = default;
};

struct AnimationFrameSetFrameDiagnostics {
  int index = 0;
  AnimationFrameSetBounds bounds;
  size_t visible_pixels = 0;
  bool contact_line_hit = false;
  // Horizontal subject-center displacement from the authored origin.
  int horizontal_anchor_drift = 0;
  // Distance from the subject's exclusive bottom bound to the contact line.
  int vertical_anchor_drift = 0;

  bool operator==(const AnimationFrameSetFrameDiagnostics& other) const = default;
};

struct AnimationFrameSetFrameResult {
  RgbaImage extracted;
  RgbaImage isolated;
  RgbaImage rasterized;
  RgbaImage finished;
  AnimationFrameSetFrameDiagnostics diagnostics;
};

struct AnimationFrameSetPipelineResult {
  int pipeline_version = kAnimationFrameSetPipelineVersion;
  std::string source_digest;
  std::string packed_digest;
  SpritePlaybackMode playback_mode = SpritePlaybackMode::kLoop;
  std::vector<AnimationFrameSetFrameResult> frames;
  RgbaImage packed_texture;
  std::vector<SpriteFrame> sprite_frames;
};

absl::Status ValidateAnimationFrameSetStyle(const AnimationFrameSetStyle& style);
absl::Status ValidateAnimationFrameSetPipelineConfig(const AnimationFrameSetPipelineConfig& config,
                                                     const AnimationFrameSetStyle& style);

// Extracts, isolates, uniformly rasterizes, quantizes, validates, and packs an
// ordered frame set. The function performs no API, filesystem, SDL, catalogue,
// or resource-manager access and returns owned values suitable for review or a
// later transactional persistence step.
absl::StatusOr<AnimationFrameSetPipelineResult> RunAnimationFrameSetPipeline(
    const RgbaImage& source, const AnimationFrameSetStyle& style,
    const AnimationFrameSetPipelineConfig& config);

}  // namespace zebes
