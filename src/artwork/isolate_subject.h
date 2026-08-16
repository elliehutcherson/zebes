#pragma once

#include "absl/status/statusor.h"
#include "common/image_io.h"

namespace zebes {

struct SubjectIsolationConfig {
  int alpha_threshold = 16;
  float background_distance = 36.0f;
  // Border-connected removal uses the broad distance above. Enclosed pixels
  // must match the estimated background much more closely before they are
  // cleared, which removes backdrop pockets inside foliage without globally
  // deleting lightly coloured subject regions.
  float enclosed_background_distance = 8.0f;
  int minimum_subject_area = 64;
  float competing_subject_ratio = 0.20f;
};

// Removes only background connected to the image border. Meaningful source
// alpha takes precedence over colour estimation.
absl::StatusOr<RgbaImage> IsolateSubject(const RgbaImage& source,
                                         const SubjectIsolationConfig& config);

}  // namespace zebes
