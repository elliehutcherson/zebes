#pragma once

#include "absl/status/statusor.h"
#include "artwork/prop_artwork.h"
#include "common/image_io.h"

namespace zebes {

struct PropCompositionConfig {
  int canvas_tiles_wide = 3;
  int canvas_tiles_high = 2;
  float padding_fraction = 0.06f;
};

// Crops or pads around the isolated subject to the requested tile aspect. The
// grounded anchor is the bottom-center of the subject, not of its canvas.
absl::StatusOr<PropArtwork> ComposeProp(const RgbaImage& isolated,
                                        const PropCompositionConfig& config);

}  // namespace zebes
