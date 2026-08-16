#pragma once

#include <vector>

#include "absl/status/statusor.h"
#include "artwork/prop_artwork.h"
#include "terrain/terrain_palette.h"

namespace zebes {

struct PropPalette {
  std::vector<RgbaColor> colors;
  RgbaColor outline;
};

// The accepted production policy uses every resolved opaque terrain colour.
absl::StatusOr<PropPalette> BuildPropPalette(const ResolvedTerrainPalette& terrain);

// Maps opaque RGB through Oklab distance. Alpha is preserved for the cleanup
// stage so resampled edge coverage remains available to edge treatment.
absl::StatusOr<PropArtwork> QuantizeProp(const PropArtwork& artwork, const PropPalette& palette);

}  // namespace zebes
