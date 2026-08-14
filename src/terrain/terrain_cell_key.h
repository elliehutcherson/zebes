#pragma once

#include <array>
#include <string>

#include "objects/tileset.h"
#include "terrain/terrain_mask.h"

namespace zebes {

// Everything a generated tile's appearance depends on, and nothing else.
//
// The atlas is a cache of TerrainRenderer, which is a pure function of a cell's
// own collision shape, its eight neighbours' shapes, and the phase of the
// periodic field. Keying that cache on anything smaller obliges the renderer to
// guess what the key left out, which is what the old AutoContext did and what
// this type exists to make unnecessary: a slope ending at air was drawn as
// buried interior purely because the 47-mask had no way to say "air is there".
//
// Neighbours are indexed by Neighbor bit position -- N, NE, E, SE, S, SW, W,
// NW. kNone is air, and a neighbour belonging to a different terrain is air
// too, because artwork stops at a material boundary.
//
// Nothing here is normalized or collapsed. Two keys that happen to render
// identically are discovered by comparing the rendered pixels, never by a rule
// asserting they must; such a rule would be a claim about the renderer that
// would have to be re-proved every time the renderer changed.
struct TerrainCellKey {
  TileShape shape = TileShape::kNone;
  std::array<TileShape, kNeighborCount> neighbors{};
  // Phase of the periodic art field. Always 0 when variant_period is 1.
  int phase = 0;

  bool operator==(const TerrainCellKey& other) const = default;

  template <typename H>
  friend H AbslHashValue(H state, const TerrainCellKey& key) {
    return H::combine(std::move(state), key.shape, key.neighbors, key.phase);
  }
};

// The neighbour mask this key implies: a bit per neighbour that is not air.
//
// This is the lossy projection the blob-47 scheme keys on, and naming it as a
// projection is the point -- a hand-drawn terrain's artwork is authored against
// exactly this much information, so for that scheme it is complete rather than
// lossy. Callers rendering generated artwork must use the whole key.
uint8_t NeighborMaskOf(const TerrainCellKey& key);

// A stable, human-readable spelling, for diagnostics and test failure output.
// Shapes are spelled with their kTileShapeIdentifiers names so a mismatch names
// what it saw instead of printing twenty integers.
std::string DebugString(const TerrainCellKey& key);

}  // namespace zebes
