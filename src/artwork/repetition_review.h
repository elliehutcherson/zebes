#pragma once

#include <cstddef>

#include "absl/status/statusor.h"
#include "common/image_io.h"

namespace zebes {

struct OpposingEdgeDifference {
  int pixels_compared = 0;
  int exact_pixel_matches = 0;
  double mean_absolute_channel_difference = 0.0;
  int maximum_channel_difference = 0;
};

struct RepetitionDiagnostics {
  OpposingEdgeDifference horizontal;
  OpposingEdgeDifference vertical;
};

// Measured edge facts for human seam review. No threshold can establish that
// an image is visually seamless, so callers must not treat this as a verdict.
absl::StatusOr<RepetitionDiagnostics> AnalyzeRepetition(const RgbaImage& image);

// Tiles the complete image without filtering. This is a transient review
// artifact, not a managed texture or pipeline output.
absl::StatusOr<RgbaImage> BuildRepetitionPreview(const RgbaImage& image, int copies_x, int copies_y,
                                                 size_t maximum_pixels);

}  // namespace zebes
