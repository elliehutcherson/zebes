#pragma once

#include "objects/level.h"

namespace zebes {

// Queries over a level's placed tiles.
//
// A level stores bare tile IDs that only mean something against the tileset it
// is bound to, so "does this level have tiles" decides whether that binding
// can still be changed. Allocated chunks never count: the editor creates them
// eagerly, so their presence says nothing about whether any ID exists.

// Whether any tile is placed.
bool LevelHasTiles(const Level& level);

// How many tiles are placed. Used to tell the user what changing the tileset
// would discard.
int CountPlacedTiles(const Level& level);

}  // namespace zebes
