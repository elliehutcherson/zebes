#pragma once

#include <string>
#include <vector>

#include "absl/strings/str_cat.h"

namespace zebes {

// Defines a single tile within a Tileset, mapping an integer ID to its visual
// region in the texture atlas and optional gameplay properties.
//
// Tile ID 0 is reserved to denote an empty cell in a TileChunk and must never
// appear in a Tileset's tile list.
struct Tile {
  // The integer ID stored in TileChunk::tiles. Must be > 0.
  int id = 0;

  // Human-readable label for editor display (e.g., "Solid Rock", "Lava").
  std::string name;

  // Pixel origin of this tile's region within the texture atlas.
  int source_x = 0;
  int source_y = 0;

  // Optional reference to a collider asset for this tile type. An empty string
  // means the tile has no collision shape.
  std::string collider_id;

  // Freeform gameplay tags applied to this tile (e.g., "solid", "lethal",
  // "one_way"). Interpreted by the physics and gameplay systems.
  std::vector<std::string> tags;
};

// A named texture atlas paired with an ordered table of tile definitions.
//
// Tilesets are immutable design-time assets. Levels reference a tileset by
// ID; TileChunk integer values are indices into the tileset's tile table.
struct Tileset {
  std::string id;
  std::string name;

  // The UUID of the texture asset that serves as the tile atlas.
  std::string texture_id;

  // Pixel dimensions of each tile cell in the atlas.
  int tile_width = 16;
  int tile_height = 16;

  // The tile definitions for this tileset. Tile ID 0 is always implicitly
  // empty and must not appear here.
  std::vector<Tile> tiles;

  std::string name_id() const { return absl::StrCat(name, "-", id); }
};

}  // namespace zebes