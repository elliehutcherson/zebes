#pragma once

#include <array>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/hash/hash.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

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
  kGentleSlopeBottomLeftLower = 10,
  kGentleSlopeBottomLeftUpper = 11,
  kGentleSlopeBottomRightLower = 12,
  kGentleSlopeBottomRightUpper = 13,

  // Ceiling variations of gentle slopes
  kGentleSlopeTopLeftLower = 14,
  kGentleSlopeTopLeftUpper = 15,
  kGentleSlopeTopRightLower = 16,
  kGentleSlopeTopRightUpper = 17,

  // --- STEEP SLOPES (1x2 Ratio, ~63.4 degrees) ---
  // It takes two vertically stacked tiles to make one steep slope.
  // "Bottom" is the tile resting on the ground.
  // "Top" is the tile above it.
  kSteepSlopeBottomLeftBottom = 18,
  kSteepSlopeBottomLeftTop = 19,
  kSteepSlopeBottomRightBottom = 20,
  kSteepSlopeBottomRightTop = 21,

  // Ceiling variations of steep slopes
  kSteepSlopeTopLeftBottom = 22,
  kSteepSlopeTopLeftTop = 23,
  kSteepSlopeTopRightBottom = 24,
  kSteepSlopeTopRightTop = 25
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
    "kGentleSlopeBottomLeftLower",
    "kGentleSlopeBottomLeftUpper",
    "kGentleSlopeBottomRightLower",
    "kGentleSlopeBottomRightUpper",
    "kGentleSlopeTopLeftLower",
    "kGentleSlopeTopLeftUpper",
    "kGentleSlopeTopRightLower",
    "kGentleSlopeTopRightUpper",
    "kSteepSlopeBottomLeftBottom",
    "kSteepSlopeBottomLeftTop",
    "kSteepSlopeBottomRightBottom",
    "kSteepSlopeBottomRightTop",
    "kSteepSlopeTopLeftBottom",
    "kSteepSlopeTopLeftTop",
    "kSteepSlopeTopRightBottom",
    "kSteepSlopeTopRightTop",
};
static_assert(std::size(kTileShapeIdentifiers) ==
                  static_cast<size_t>(TileShape::kSteepSlopeTopRightTop) + 1,
              "kTileShapeIdentifiers must name every TileShape");

// Resolves an identifier back to its shape. Returns nullopt for unknown names so
// callers can fail loudly rather than defaulting to kNone.
inline std::optional<TileShape> TileShapeFromIdentifier(std::string_view identifier) {
  for (size_t i = 0; i < std::size(kTileShapeIdentifiers); ++i) {
    if (identifier == kTileShapeIdentifiers[i]) return static_cast<TileShape>(i);
  }
  return std::nullopt;
}

// Bit positions describing where a cell's eight neighbours sit.
//
// This is adjacency, which is data about tiles, so it lives here beside
// TileShape rather than in the blob-47 module that first needed it. The offline
// atlas tools, the level editor brush and the serialized formats all share it,
// so none of them can disagree about which direction bit 3 means.
enum Neighbor : uint8_t {
  kNorth = 1 << 0,
  kNorthEast = 1 << 1,
  kEast = 1 << 2,
  kSouthEast = 1 << 3,
  kSouth = 1 << 4,
  kSouthWest = 1 << 5,
  kWest = 1 << 6,
  kNorthWest = 1 << 7,
};

inline constexpr int kNeighborCount = 8;

// Where each neighbour sits relative to a cell, indexed by the bit position
// above. Screen space, so negative y is north. This is the only definition.
struct NeighborOffset {
  int dx = 0;
  int dy = 0;
};

inline constexpr NeighborOffset kNeighborOffsets[kNeighborCount] = {
    {.dx = 0, .dy = -1}, {.dx = 1, .dy = -1}, {.dx = 1, .dy = 0},  {.dx = 1, .dy = 1},
    {.dx = 0, .dy = 1},  {.dx = -1, .dy = 1}, {.dx = -1, .dy = 0}, {.dx = -1, .dy = -1},
};

// Everything a generated tile's appearance depends on, and nothing else.
//
// Artwork for a derived terrain is a pure function of a cell's own collision
// shape, its eight neighbours' shapes, and the phase of the periodic field.
// Keying that on anything smaller obliges the renderer to guess what the key
// left out, which is how a slope ending at open air came to be drawn as buried
// interior: the 47-mask had no way to say "air is there".
//
// Neighbours are indexed by Neighbor bit position. kNone is air, and a
// neighbour belonging to a different terrain is air too, because artwork stops
// at a material boundary.
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

// The neighbour mask a key implies: a bit per neighbour that is not air.
//
// This is the lossy projection the blob-47 scheme keys on, and naming it as a
// projection is the point. A hand-drawn terrain's artwork is authored against
// exactly this much information, so for that scheme it is complete rather than
// lossy; a derived terrain must use the whole key.
inline uint8_t NeighborMaskOf(const TerrainCellKey& key) {
  uint8_t mask = 0;
  for (int i = 0; i < kNeighborCount; ++i) {
    if (key.neighbors[i] != TileShape::kNone) mask |= static_cast<uint8_t>(1 << i);
  }
  return mask;
}

// A stable, readable spelling, for diagnostics and test failure output. Shapes
// are named so a mismatch says what it saw instead of printing ten integers.
inline std::string DebugString(const TerrainCellKey& key) {
  std::vector<std::string> neighbors;
  neighbors.reserve(kNeighborCount);
  for (const TileShape neighbor : key.neighbors) {
    neighbors.push_back(kTileShapeIdentifiers[static_cast<size_t>(neighbor)]);
  }
  return absl::StrCat(kTileShapeIdentifiers[static_cast<size_t>(key.shape)], " phase ", key.phase,
                      " [", absl::StrJoin(neighbors, " "), "]");
}

// A tile a derived terrain rendered, paired with the neighbourhood it depicts.
struct DerivedTile {
  int tile_id = 0;
  TerrainCellKey key;

  bool operator==(const DerivedTile& other) const = default;
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
  // Artwork is derived from a recipe rather than authored, so it is rendered
  // for whatever neighbourhood a level actually contains instead of being
  // enumerated ahead of time. Such a terrain has no rule table: a mask cannot
  // express that a neighbour is a wedge, and baking against one is what left a
  // slope meeting open air drawn as buried interior.
  kDerived = 1,
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

  // kBlob47 only. Tiles this terrain owns that its mask-keyed rules do not
  // produce: authored slope units and hand-placed set-pieces. Paired with
  // Tile::shape this is the scheme's shape-to-artwork table, and it is also
  // what makes painted ground continue into a slope instead of capping off
  // with an edge against it.
  std::vector<int> shape_tile_ids;

  // kDerived only. Every tile this terrain has had rendered, each with the
  // neighbourhood it depicts.
  //
  // The key is what the renderer was given, so it is what has to be given again
  // to redraw the tile after the recipe changes. Nothing else records it: a
  // Tile knows where its pixels are and what it collides as, not what it is a
  // picture of. Without this, retuning a material could only redraw the tiles
  // generation happened to produce and would leave every tile a level asked for
  // showing the old artwork.
  std::vector<DerivedTile> derived_tiles;

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