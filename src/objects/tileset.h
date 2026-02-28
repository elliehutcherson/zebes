#pragma once

#include <string>
#include <vector>

#include "absl/strings/str_cat.h"

namespace zebes {

enum TileShape : uint8_t {
  kNone = 0,
  kFullBlock = 1,

  // --- HALF BLOCKS ---
  // Useful for pass-through platforms or thin walls.
  kHalfBlockBottom = 2,
  kHalfBlockTop = 3,
  kHalfBlockLeft = 4,
  kHalfBlockRight = 5,

  // --- 45-DEGREE SLOPES (1x1 Ratio) ---
  // The name indicates where the "solid" right-angle corner of the triangle is.
  kSlope45BottomLeft = 6,   // /| shape (walking up to the right)
  kSlope45BottomRight = 7,  // |\ shape (walking up to the left)
  kSlope45TopLeft = 8,      // \| shape (ceiling slope)
  kSlope45TopRight = 9,     // |/ shape (ceiling slope)

  // --- GENTLE SLOPES (2x1 Ratio, ~26.5 degrees) ---
  // It takes two adjacent tiles to make one smooth gentle slope.
  // "Lower" means the wedge that starts from 0 height.
  // "Upper" means the wedge that connects to the top of the tile.
  kGentleSlopeBottomLeft_Lower = 10,
  kGentleSlopeBottomLeft_Upper = 11,
  kGentleSlopeBottomRight_Lower = 12,
  kGentleSlopeBottomRight_Upper = 13,

  // Ceiling variations of gentle slopes
  kGentleSlopeTopLeft_Lower = 14,
  kGentleSlopeTopLeft_Upper = 15,
  kGentleSlopeTopRight_Lower = 16,
  kGentleSlopeTopRight_Upper = 17,

  // --- STEEP SLOPES (1x2 Ratio, ~63.4 degrees) ---
  // It takes two vertically stacked tiles to make one steep slope.
  // "Bottom" is the tile resting on the ground.
  // "Top" is the tile above it.
  kSteepSlopeBottomLeft_Bottom = 18,
  kSteepSlopeBottomLeft_Top = 19,
  kSteepSlopeBottomRight_Bottom = 20,
  kSteepSlopeBottomRight_Top = 21,

  // Ceiling variations of steep slopes
  kSteepSlopeTopLeft_Bottom = 22,
  kSteepSlopeTopLeft_Top = 23,
  kSteepSlopeTopRight_Bottom = 24,
  kSteepSlopeTopRight_Top = 25
};

struct Tile {
  int id = 0;
  std::string name;
  int source_x = 0;
  int source_y = 0;

  // The mathematical shape of the tile.
  TileShape shape = TileShape::kNone;

  // If true, entities can pass through this tile when moving upwards or horizontally,
  // but will collide when falling downwards onto it.
  bool is_one_way = false;

  // Keep tags for high-level gameplay logic (e.g., "lethal", "ice", "water")
  // which are checked less frequently than physical collisions.
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