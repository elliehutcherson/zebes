#pragma once

#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/types/span.h"
#include "objects/tileset.h"

namespace zebes {

// One thing a person means when they place terrain.
//
// A unit is deliberately not a TileShape. Gentle and steep ramps are two cells
// -- two halves of one ramp, side by side or stacked -- and a half placed
// without its partner is broken collision rather than a smaller ramp. Because
// shape *is* collision, that is a real defect and not a cosmetic one, and it is
// reachable today: the Tiles palette lists the halves separately as `Slope10`,
// `Slope11` and so on. Offering units instead of enumerators removes it, and
// turns twenty slope shapes into about ten things worth naming.
struct TerrainPlacementUnit {
  // Shown in the palette. Names the ramp, not the enumerator.
  std::string name;
  int width = 1;
  int height = 1;
  // Row-major, width * height entries. Never kNone: a unit is solid everywhere
  // it claims a cell.
  std::vector<TileShape> cells;

  // The shape at a cell offset within the unit.
  TileShape At(int x, int y) const;

  bool operator==(const TerrainPlacementUnit& other) const = default;
};

// Every unit TileShape can express, in palette order: the block, the four half
// blocks, then the 45-degree, gentle and steep ramps.
//
// This is the catalogue of what is expressible, not what any one terrain can
// place -- see PlacementUnitsWithin.
absl::Span<const TerrainPlacementUnit> AllTerrainPlacementUnits();

// The catalogue filtered to units whose every cell has artwork available.
//
// A derived terrain renders on demand and passes every shape, so it gets the
// whole catalogue. A blob-47 terrain passes only the shapes it has tiles for,
// so a picker built from this can never offer a ramp whose second half would
// render nothing.
std::vector<TerrainPlacementUnit> PlacementUnitsWithin(
    const absl::flat_hash_set<TileShape>& available);

}  // namespace zebes
