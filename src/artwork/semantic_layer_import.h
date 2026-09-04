#pragma once

#include <cstddef>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/image_io.h"

namespace zebes {

// Placement of one cropped semantic RGBA layer in its model-output canvas.
struct SemanticLayerCrop {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// One alpha-connected semantic component retained on the common canvas. Bounds
// are inclusive at the minimum and exclusive at the maximum.
struct SemanticLayerComponent {
  RgbaImage artwork;
  int minimum_x = 0;
  int minimum_y = 0;
  int maximum_x = 0;
  int maximum_y = 0;
  size_t pixel_count = 0;
};

struct SemanticVisibleOwnership {
  size_t source_pixels = 0;
  size_t singly_owned_pixels = 0;
  size_t unowned_pixels = 0;
  size_t multiply_owned_pixels = 0;
  size_t ownership_outside_source_pixels = 0;
};

struct SemanticLayerMutation {
  size_t changed_pixels = 0;
  size_t alpha_added_pixels = 0;
  size_t alpha_removed_pixels = 0;
};

// Restores a model-produced cropped layer to its declared full canvas. The crop
// and image dimensions must agree exactly; clipping or implicit scaling fails.
absl::StatusOr<RgbaImage> RestoreSemanticLayer(const RgbaImage& cropped,
                                               const SemanticLayerCrop& crop, int canvas_width,
                                               int canvas_height);

// Reduces a semantic layer by an exact integer factor. RGB is averaged with
// alpha weighting and output alpha is binary, preventing transparent padding
// colors from contaminating completed artwork.
absl::StatusOr<RgbaImage> DownsampleSemanticLayer(const RgbaImage& source, int target_width,
                                                  int target_height,
                                                  double coverage_threshold = 0.2);

// Replaces every candidate pixel selected by visible_mask with the corresponding
// original source pixel. Selected transparent source pixels are rejected: the
// mask must describe authoritative visible artwork, not hidden completion.
absl::StatusOr<RgbaImage> PreserveSemanticVisiblePixels(const RgbaImage& candidate,
                                                        const RgbaImage& source,
                                                        const RgbaImage& visible_mask);

// Clears candidate pixels outside allowed_mask. RGB inside the retained alpha
// is unchanged; this bounds inferred hidden artwork to an explicit amodal
// region before it participates in composition.
absl::StatusOr<RgbaImage> ClipSemanticLayerToMask(const RgbaImage& candidate,
                                                  const RgbaImage& allowed_mask);

// Counts exclusive visible ownership against the approved source. A complete
// decomposition has every source pixel singly owned, no overlap, and no visible
// ownership outside the source alpha.
absl::StatusOr<SemanticVisibleOwnership> MeasureSemanticVisibleOwnership(
    const RgbaImage& source, absl::Span<const RgbaImage> visible_layers);

// Measures any decoded RGBA or silhouette change between an accepted semantic
// source and a processed layer. Immutable artwork passes only when every count
// is zero.
absl::StatusOr<SemanticLayerMutation> MeasureSemanticLayerMutation(const RgbaImage& source,
                                                                   const RgbaImage& processed);

// Splits nontransparent artwork into deterministic four-connected components,
// discards components smaller than minimum_pixels, and returns the remainder in
// left-to-right then top-to-bottom order.
absl::StatusOr<std::vector<SemanticLayerComponent>> SplitSemanticLayerComponents(
    const RgbaImage& source, size_t minimum_pixels = 1);

}  // namespace zebes
