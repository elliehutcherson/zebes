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
  // Which family the piece belongs to, so a palette can lay the catalogue out
  // in rows instead of as one list of twenty-five.
  //
  // It matters most for the two-cell families: a gentle or steep ramp is built
  // from a pair, and a flat list gives no way to see that the halves belong
  // together. Grouping puts them side by side, where the pairing is visible.
  std::string group;

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

// The shapes `terrain` can put on screen.
//
// A derived terrain renders on demand, so every shape is available whether or
// not its artwork has been drawn yet. An authored one can only place artwork
// that exists, and this reads that off its tiles rather than assuming a set, so
// a tileset missing a piece is a palette that does not offer it instead of a
// palette that offers a blank.
absl::flat_hash_set<TileShape> PaintableShapesOf(const Terrain& terrain, const Tileset& tileset);

// The tile that best represents a terrain in a palette, or null when it has
// drawn none.
//
// The interior of a filled region: a block with the same material on all eight
// sides, which reads as the material itself rather than one of its edges.
//
// The two schemes record that tile in different places and there is no shared
// one to read. A blob-47 terrain has a rule for the fully surrounded mask; a
// derived terrain deliberately has no rule table at all, and instead has a
// derived tile whose key says the same thing. Reading only the rules found
// nothing for every derived terrain, which is why they all drew a blank.
const Tile* TerrainSwatchTile(const Tileset& tileset, const Terrain& terrain);

}  // namespace zebes
