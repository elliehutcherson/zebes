#pragma once

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "artwork/prop_artwork.h"
#include "terrain/terrain_palette.h"

namespace zebes {

struct PropCleanupConfig {
  int alpha_threshold = 128;
  int minimum_component_area = 2;
  int tile_size = 32;
  int grounded_tolerance = 3;
};

// Makes alpha binary, removes only explicitly small components, and verifies
// the finished prop contract.
absl::StatusOr<PropArtwork> CleanupAndValidateProp(const PropArtwork& artwork,
                                                   absl::Span<const RgbaColor> palette,
                                                   const PropCleanupConfig& config);

}  // namespace zebes
