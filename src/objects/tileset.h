#pragma once

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
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
  // "Bottom"/"Top" is the edge the solid mass hugs; "Left"/"Right" is the side
  // the wedge tapers away to nothing on, so the right angle sits at the
  // opposite corner. The exact polygons live in objects/tile_shape_geometry.h.
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

// Stable identifiers for TileShape, matching the enumerator spellings and
// indexed by numeric value.
//
// These are a tool contract: asset pipelines parse them off the command line
// and print them in manifests, so unlike the editor's human-facing display
// strings they must never be reworded. They live beside the enum so that adding
// a shape without naming it fails to compile.
inline constexpr const char* kTileShapeIdentifiers[] = {
    "kNone",
    "kFullBlock",
    "kHalfBlockBottom",
    "kHalfBlockTop",
    "kHalfBlockLeft",
    "kHalfBlockRight",
    "kSlope45BottomLeft",
    "kSlope45BottomRight",
    "kSlope45TopLeft",
    "kSlope45TopRight",
    "kGentleSlopeBottomLeft_Lower",
    "kGentleSlopeBottomLeft_Upper",
    "kGentleSlopeBottomRight_Lower",
    "kGentleSlopeBottomRight_Upper",
    "kGentleSlopeTopLeft_Lower",
    "kGentleSlopeTopLeft_Upper",
    "kGentleSlopeTopRight_Lower",
    "kGentleSlopeTopRight_Upper",
    "kSteepSlopeBottomLeft_Bottom",
    "kSteepSlopeBottomLeft_Top",
    "kSteepSlopeBottomRight_Bottom",
    "kSteepSlopeBottomRight_Top",
    "kSteepSlopeTopLeft_Bottom",
    "kSteepSlopeTopLeft_Top",
    "kSteepSlopeTopRight_Bottom",
    "kSteepSlopeTopRight_Top",
};
static_assert(std::size(kTileShapeIdentifiers) ==
                  static_cast<size_t>(TileShape::kSteepSlopeTopRight_Top) + 1,
              "kTileShapeIdentifiers must name every TileShape");

// Resolves an identifier back to its shape. Returns nullopt for unknown names so
// callers can fail loudly rather than defaulting to kNone.
inline std::optional<TileShape> TileShapeFromIdentifier(std::string_view identifier) {
  for (size_t i = 0; i < std::size(kTileShapeIdentifiers); ++i) {
    if (identifier == kTileShapeIdentifiers[i]) return static_cast<TileShape>(i);
  }
  return std::nullopt;
}

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

  // Value equality over every field. The editor compares an in-progress tileset
  // against the copy it started from to know whether closing would discard
  // work, so equality has to mean "identical in every authored respect".
  bool operator==(const Tile& other) const = default;
};

// One tile eligible for a neighbour mask. Listing several variants for the same
// mask is what keeps a large painted region from repeating a single tile.
struct TerrainVariant {
  int tile_id = 0;

  // Relative likelihood among the variants of one rule. Must be positive.
  int weight = 1;

  bool operator==(const TerrainVariant& other) const = default;
};

// The tiles eligible for one normalized neighbour mask.
struct TerrainRule {
  uint8_t mask = 0;
  std::vector<TerrainVariant> variants;

  bool operator==(const TerrainRule& other) const = default;
};

// How a terrain's rule table is indexed. Smaller schemes expand into the same
// mask-keyed table rather than changing how rules are stored, so introducing
// one never migrates level data.
enum class TerrainScheme : uint8_t {
  kBlob47 = 0,
};

// A group of tiles the brush treats as one material. Painting a terrain writes
// whichever tile matches the painted cell's neighbourhood, so the artwork is
// resolved once at paint time and levels keep storing plain tile IDs.
struct Terrain {
  int id = 0;
  std::string name;
  TerrainScheme scheme = TerrainScheme::kBlob47;

  // Whether cells outside the level bounds count as this terrain. True keeps
  // ground continuous at the level border instead of drawing an edge there.
  bool solid_outside_level = true;

  // How many tiles the artwork's pattern takes to repeat, on both axes.
  //
  // Zero means the variants are interchangeable and one is picked per cell by a
  // hash of its coordinates: unstructured variety, any number of variants.
  //
  // A positive P means the variants are P x P phases of one larger pattern, and
  // the cell at (x, y) must use phase (y mod P) * P + (x mod P) or the pattern
  // will not line up across tile borders. Every rule then needs exactly P * P
  // variants. This is what lets generated artwork carry a surface pattern
  // longer than a single tile without seams.
  int variant_period = 0;

  // Unique by mask, ascending.
  std::vector<TerrainRule> rules;

  // Tiles that count as this terrain when computing a neighbour mask but that
  // the brush never writes. Slope units and hand-placed set-pieces live here,
  // so painted ground continues into them instead of capping off with an edge.
  // A tile must not appear both here and in rules.
  std::vector<int> member_tile_ids;

  bool operator==(const Terrain& other) const = default;
};

// A named texture atlas paired with an ordered table of tile definitions.
//
// Tilesets are immutable design-time assets. Levels reference a tileset by ID;
// TileChunk integer values are tile IDs, not positions in the table below.
// viewport_scene.cc resolves them through a lookup keyed on Tile::id and fails
// with "level references unknown tile ID" when one is missing, so reordering
// the table never invalidates a level but deleting a tile does.
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

  // Terrain brushes defined over the tiles above. Empty for tilesets that are
  // only placed by hand.
  std::vector<Terrain> terrains;

  bool operator==(const Tileset& other) const = default;

  std::string name_id() const { return absl::StrCat(name, "-", id); }
};

}  // namespace zebes