#pragma once

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "artwork/prop_artwork.h"
#include "terrain/terrain_palette.h"

namespace zebes {

enum class PropPalettePolicy : uint8_t {
  kFullTerrain = 0,
  kSemanticSubset = 1,
  kDerivedRamps = 2,
};

struct PropPalette {
  PropPalettePolicy policy = PropPalettePolicy::kFullTerrain;
  std::vector<RgbaColor> colors;
  RgbaColor outline;
};

absl::StatusOr<PropPalette> BuildPropPalette(const ResolvedTerrainPalette& terrain,
                                             const TerrainMaterial& material,
                                             PropPalettePolicy policy);

// Maps opaque RGB through Oklab distance. Alpha is preserved for the cleanup
// stage so resampled edge coverage remains available to edge treatment.
absl::StatusOr<PropArtwork> QuantizeProp(const PropArtwork& artwork, const PropPalette& palette);

}  // namespace zebes
