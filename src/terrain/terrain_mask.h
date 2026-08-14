#pragma once

#include <cstdint>
#include <optional>

#include "absl/types/span.h"

namespace zebes {

// Bit positions describing which of a cell's eight neighbours belong to the
// same terrain. The offline atlas tools and the level editor brush share this
// header so generated artwork and painted levels can never disagree about what
// a mask means.
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
// above. Screen space, so negative y is north.
//
// This is the only definition. The generator walks a 3x3 canvas and the brush
// walks a level's tile grid, but if the two ever disagreed about which
// direction bit 3 meant, generated artwork and painted levels would silently
// stop matching -- which is precisely the failure this header exists to
// prevent.
struct NeighborOffset {
  int dx = 0;
  int dy = 0;
};

inline constexpr NeighborOffset kNeighborOffsets[kNeighborCount] = {
    {.dx = 0, .dy = -1},  {.dx = 1, .dy = -1}, {.dx = 1, .dy = 0},  {.dx = 1, .dy = 1},
    {.dx = 0, .dy = 1},   {.dx = -1, .dy = 1}, {.dx = -1, .dy = 0}, {.dx = -1, .dy = -1},
};

// Distinct masks surviving normalization, and the atlas grid the offline tools
// lay them out in. 47 of the 48 cells are used.
inline constexpr int kBlob47TileCount = 47;
inline constexpr int kBlob47Columns = 8;
inline constexpr int kBlob47Rows = 6;

// Clears every corner bit whose two flanking edges are not both set. A diagonal
// neighbour is only visible when the cell also connects to it around the
// outside, which collapses the 256 raw masks onto 47 distinct appearances.
uint8_t NormalizeNeighborMask(uint8_t mask);

// The normalized masks in ascending order. Atlas index i holds the artwork for
// mask Blob47MaskTable()[i]; this ordering is the contract between the
// compositor, the tileset importer, and the brush.
absl::Span<const uint8_t> Blob47MaskTable();

// Inverse of Blob47MaskTable(). Returns nullopt when mask is not normalized,
// which callers should treat as a programming error rather than a fallback.
std::optional<int> Blob47IndexForMask(uint8_t normalized_mask);

// One corner of a tile. Every blob-47 tile is four quadrants, which is what
// lets 20 authored sprites generate the whole 47-tile set.
enum class Quadrant : uint8_t {
  kNorthWest = 0,
  kNorthEast = 1,
  kSouthEast = 2,
  kSouthWest = 3,
};

inline constexpr int kQuadrantCount = 4;

// The five appearances a quadrant can take. Which one applies depends only on
// the quadrant's two flanking edges and its diagonal, so five sprites per
// corner cover every tile in the set.
//
// Naming is relative to the quadrant. For the north-west quadrant the flanking
// edges are North (vertical) and West (horizontal):
//
//   kOuterCorner     both sides exposed        kEdgeVertical   left side exposed
//     +------            +------                 |#####           |#####
//     |  ####            |                       |#####           |#####
//     |  ####            |  ####                 |#####           |#####
//
//   kEdgeHorizontal  top side exposed          kInnerCorner    diagonal absent
//     ------                                     ####|
//     ######                                     ####|
//     ######                                     ----+
enum class QuadrantState : uint8_t {
  // Neither flanking edge is covered: an outward-facing convex corner.
  kOuterCorner = 0,
  // Only the vertically adjacent edge is covered, leaving a vertical wall face.
  kEdgeVertical = 1,
  // Only the horizontally adjacent edge is covered, leaving a flat surface.
  kEdgeHorizontal = 2,
  // Both flanking edges covered but the diagonal absent: a concave corner.
  // This is the state a 3x3 tile block cannot express.
  kInnerCorner = 3,
  // Both flanking edges and the diagonal covered: solid interior.
  kFill = 4,
};

inline constexpr int kQuadrantStateCount = 5;

// Returns the appearance of quadrant within a cell whose neighbourhood is
// normalized_mask. The mask must already be normalized; passing a raw mask can
// report kFill where the artwork requires kInnerCorner.
QuadrantState QuadrantStateForMask(uint8_t normalized_mask, Quadrant quadrant);

}  // namespace zebes
