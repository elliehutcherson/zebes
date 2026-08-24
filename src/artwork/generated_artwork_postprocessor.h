#pragma once

#include <cstddef>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "terrain/terrain_palette.h"

namespace zebes {

enum class GeneratedArtworkBackgroundPolicy {
  kPreserve = 0,
  // Removes tolerant exterior matte and enclosed regions seeded by pixels
  // matching the explicitly configured background color.
  kRemoveSolidMatte = 1,
};

enum class GeneratedArtworkPalettePolicy {
  kPreserve = 0,
  kQuantize = 1,
};

enum class GeneratedArtworkAlphaPolicy {
  kPreserve = 0,
  kBinary = 1,
  kOpaque = 2,
};

struct GeneratedArtworkPostprocessConfig {
  int output_width = 960;
  int output_height = 540;
  int pixel_block_size = 1;
  GeneratedArtworkBackgroundPolicy background_policy =
      GeneratedArtworkBackgroundPolicy::kRemoveSolidMatte;
  GeneratedArtworkPalettePolicy palette_policy = GeneratedArtworkPalettePolicy::kQuantize;
  GeneratedArtworkAlphaPolicy alpha_policy = GeneratedArtworkAlphaPolicy::kBinary;
  RgbaColor background = {255, 0, 255, 255};
  float transparent_distance = 24.0f;
  float opaque_distance = 190.0f;
  int final_alpha_threshold = 128;
  int palette_alpha_threshold = 128;
  int maximum_palette_colors = 64;
  int minimum_visible_pixels = 64;
  int minimum_transparent_border = 1;
};

struct GeneratedArtworkBounds {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

struct GeneratedArtworkPostprocessDiagnostics {
  int palette_colors = 0;
  size_t visible_pixels = 0;
  size_t partially_matted_pixels = 0;
  GeneratedArtworkBounds visible_bounds;
};

struct GeneratedArtworkPostprocessResult {
  RgbaImage matted;
  RgbaImage resized;
  RgbaImage finished;
  std::vector<RgbaColor> palette;
  GeneratedArtworkPostprocessDiagnostics diagnostics;
};

absl::Status ValidateGeneratedArtworkPostprocessConfig(
    const GeneratedArtworkPostprocessConfig& config);

// Extracts exact opaque colors from a reference image, ordered by descending
// use count and then RGBA value for deterministic quantization ties.
absl::StatusOr<std::vector<RgbaColor>> ExtractGeneratedArtworkPalette(const RgbaImage& reference,
                                                                      int alpha_threshold,
                                                                      int maximum_colors);

absl::StatusOr<RgbaImage> ResizeGeneratedArtwork(const RgbaImage& source, int output_width,
                                                 int output_height, int pixel_block_size = 1);

absl::StatusOr<GeneratedArtworkPostprocessResult> PostprocessGeneratedArtwork(
    const RgbaImage& source, const std::vector<RgbaColor>& palette,
    const GeneratedArtworkPostprocessConfig& config);

// Extracts a palette from the reference and applies the configured processing
// policies. Solid-matte removal decontaminates boundary pixels before the
// premultiplied resizer and optional Oklab quantizer run.
absl::StatusOr<GeneratedArtworkPostprocessResult> PostprocessGeneratedArtwork(
    const RgbaImage& source, const RgbaImage& palette_reference,
    const GeneratedArtworkPostprocessConfig& config);

}  // namespace zebes
