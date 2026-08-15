#pragma once

#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/types/span.h"
#include "objects/tileset.h"

namespace zebes {

// A shape the terrain palette can offer, named for what the player walks on
// rather than for what the polygon tapers to.
//
// One choice paints one cell. Longer ramps are built by placing their parts,
// and the parts compose freely because their edge heights line up: a gentle
// ramp's lower half ends at half tile height, a flat half block sits at half
// tile height all the way across, and the upper half starts there. So
//
//     lower half -> half block -> half block -> upper half
//
// is one continuous surface with a landing in the middle, and needs no support
// beyond being able to place each piece.
//
// There is deliberately no multi-cell footprint here. Treating a gentle ramp as
// one two-cell stamp would make exactly the arrangement above unreachable,
// which is a real thing to want and costs nothing to allow.
struct TerrainShapeChoice {
  TileShape shape = TileShape::kNone;
  // "lower"/"upper" say which end of a two-cell ramp a half is: the lower half
  // starts at floor level, the upper half reaches the top of its cell. That is
  // what the TileShape enumerators mean, spelled for someone choosing one.
  std::string name;

  bool operator==(const TerrainShapeChoice& other) const = default;
};

// Every shape TileShape can express, in palette order: the block, half blocks,
// then the 45-degree, gentle and steep pieces.
//
// This is the catalogue of what is expressible, not what any one terrain can
// paint -- see ShapeChoicesWithin.
absl::Span<const TerrainShapeChoice> AllTerrainShapeChoices();

// The catalogue filtered to shapes a terrain has artwork for.
//
// A derived terrain renders on demand and passes every shape, so it gets the
// whole catalogue. A blob-47 terrain passes only the shapes it holds tiles for,
// so a palette built from this can never offer a piece that would render
// nothing.
std::vector<TerrainShapeChoice> ShapeChoicesWithin(
    const absl::flat_hash_set<TileShape>& available);

}  // namespace zebes
