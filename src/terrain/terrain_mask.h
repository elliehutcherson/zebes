#pragma once

#include <cstdint>
#include <optional>

#include "absl/types/span.h"
#include "objects/tileset.h"

namespace zebes {

// The Neighbor bit layout and kNeighborOffsets live in objects/tileset.h.
// Adjacency is data about tiles and is serialized as part of a derived
// terrain's keys; what stays here is the blob-47 scheme built on top of it.

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
