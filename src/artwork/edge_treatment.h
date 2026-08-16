#pragma once

#include "absl/status/statusor.h"
#include "artwork/prop_artwork.h"
#include "terrain/terrain_palette.h"

namespace zebes {

struct PropEdgeConfig {
  int width = 1;
  int alpha_threshold = 128;
};

// Recolours the opaque inside boundary without changing alpha or expanding the
// silhouette.
absl::StatusOr<PropArtwork> ApplyPropEdgeTreatment(const PropArtwork& artwork,
                                                   const RgbaColor& outline,
                                                   const PropEdgeConfig& config);

}  // namespace zebes
