#pragma once

#include <cstdint>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "terrain/terrain_style.h"

namespace zebes {

// A motif pixel names its visual role rather than a renderer palette index.
// This prevents motif data from depending on the private ordering of the final
// palette and makes unsupported values impossible to express accidentally.
enum class TerrainMotifPixel : uint8_t {
  kTransparent = 0,
  kAutoShaded = 1,
  kDecor = 2,
  kDecorShade = 3,
  kBotanical = 4,
  kBotanicalShade = 5,
  kAccentPrimary = 6,
  kAccentSecondary = 7,
};

struct TerrainMotif {
  int width = 0;
  int height = 0;
  absl::Span<const TerrainMotifPixel> pixels;
};

// Substrate marks and semantic objects use separate banks even though they
// share placement machinery. Changing the dirt pattern must not quietly alter
// the flower population, and vice versa.
absl::Span<const TerrainMotif> TerrainSubstrateMotifsFor(TerrainSubstratePattern pattern,
                                                         TerrainPixelProfile profile);
absl::Span<const TerrainMotif> TerrainDetailMotifsFor(TerrainDetailSet detail_set,
                                                      TerrainPixelProfile profile);
absl::Status ValidateTerrainMotifs(absl::Span<const TerrainMotif> motifs);

}  // namespace zebes
