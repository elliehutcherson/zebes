#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/repetition_review.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "terrain/terrain_palette.h"

namespace zebes {

inline constexpr int kParallaxArtworkPipelineVersion = 1;

enum class ParallaxArtworkFramePolicy : uint8_t {
  kCropToFill = 0,
  kFitInside = 1,
};

enum class ParallaxArtworkAlphaRole : uint8_t {
  kOpaquePlate = 0,
  kTransparentOverlay = 1,
};

enum class ParallaxArtworkOverlayExtraction : uint8_t {
  kPreserveAlpha = 0,
  // Removes exterior matte plus enclosed openings containing the configured
  // matte color; isolated merely similar foreground colors remain intact.
  kRemoveSolidMatte = 1,
};

enum class ParallaxArtworkOverlayAlphaPolicy : uint8_t {
  kPreserve = 0,
  kBinary = 1,
};

struct ParallaxArtworkStyle {
  int pixel_block_size = 1;
  bool quantize_to_palette = true;
  // Resolved opaque colours, retained by value so regeneration does not
  // change when an attached terrain recipe is later edited.
  std::vector<RgbaColor> palette;
};

struct ParallaxArtworkPipelineConfig {
  SourceArtworkLimits source_limits;
  int target_width = 960;
  int target_height = 540;
  ParallaxArtworkFramePolicy frame_policy = ParallaxArtworkFramePolicy::kCropToFill;
  ParallaxArtworkAlphaRole alpha_role = ParallaxArtworkAlphaRole::kOpaquePlate;
  ParallaxArtworkOverlayExtraction overlay_extraction =
      ParallaxArtworkOverlayExtraction::kPreserveAlpha;
  ParallaxArtworkOverlayAlphaPolicy overlay_alpha_policy =
      ParallaxArtworkOverlayAlphaPolicy::kPreserve;
  RgbaColor matte_color = {255, 0, 255, 255};
  float matte_transparent_distance = 24.0f;
  float matte_opaque_distance = 190.0f;
  int binary_alpha_threshold = 128;
  bool review_repeat_x = false;
  bool review_repeat_y = false;
};

enum class ParallaxArtworkStage : uint8_t {
  kFraming = 0,
  kMatteExtraction = 1,
  kRasterization = 2,
  kFinished = 3,
};

struct ParallaxArtworkStageDiagnostic {
  ParallaxArtworkStage stage = ParallaxArtworkStage::kFraming;
  int width = 0;
  int height = 0;
  size_t visible_pixels = 0;
};

struct ParallaxArtworkPipelineResult {
  int pipeline_version = kParallaxArtworkPipelineVersion;
  std::string source_digest;
  std::string final_digest;
  RgbaImage framed;
  RgbaImage matte_extracted;
  RgbaImage rasterized;
  RgbaImage finished;
  std::array<ParallaxArtworkStageDiagnostic, 4> stages;
  RepetitionDiagnostics repetition;
  std::optional<RgbaImage> repeat_x_preview;
  std::optional<RgbaImage> repeat_y_preview;
};

absl::Status ValidateParallaxArtworkStyle(const ParallaxArtworkStyle& style);
absl::Status ValidateParallaxArtworkPipelineConfig(const ParallaxArtworkPipelineConfig& config,
                                                   const ParallaxArtworkStyle& style);

absl::StatusOr<ParallaxArtworkPipelineResult> RunParallaxArtworkPipeline(
    const RgbaImage& source, const ParallaxArtworkStyle& style,
    const ParallaxArtworkPipelineConfig& config);

}  // namespace zebes
