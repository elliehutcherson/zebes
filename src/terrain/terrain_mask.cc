#include "terrain/terrain_mask.h"

#include <algorithm>
#include <array>
#include <vector>

namespace zebes {
namespace {

// The three neighbour bits that determine one quadrant's appearance.
struct QuadrantBits {
  uint8_t vertical_edge;
  uint8_t horizontal_edge;
  uint8_t diagonal;
};

QuadrantBits BitsForQuadrant(Quadrant quadrant) {
  switch (quadrant) {
    case Quadrant::kNorthWest:
      return {.vertical_edge = kNorth, .horizontal_edge = kWest, .diagonal = kNorthWest};
    case Quadrant::kNorthEast:
      return {.vertical_edge = kNorth, .horizontal_edge = kEast, .diagonal = kNorthEast};
    case Quadrant::kSouthEast:
      return {.vertical_edge = kSouth, .horizontal_edge = kEast, .diagonal = kSouthEast};
    case Quadrant::kSouthWest:
      return {.vertical_edge = kSouth, .horizontal_edge = kWest, .diagonal = kSouthWest};
  }
  return {.vertical_edge = kNorth, .horizontal_edge = kWest, .diagonal = kNorthWest};
}

// Builds the ascending table of normalized masks. Constructed once and cached
// because both the mask table and its inverse are read per painted cell.
std::vector<uint8_t> BuildBlob47Masks() {
  std::array<bool, 256> seen{};
  std::vector<uint8_t> masks;
  masks.reserve(kBlob47TileCount);

  for (int raw_mask = 0; raw_mask < 256; ++raw_mask) {
    const uint8_t mask = NormalizeNeighborMask(static_cast<uint8_t>(raw_mask));
    if (seen[mask]) continue;
    seen[mask] = true;
    masks.push_back(mask);
  }

  std::sort(masks.begin(), masks.end());
  return masks;
}

// Inverse of the mask table. Entries for non-normalized masks stay at -1.
std::array<int, 256> BuildMaskIndex(const std::vector<uint8_t>& masks) {
  std::array<int, 256> index;
  index.fill(-1);
  for (int i = 0; i < static_cast<int>(masks.size()); ++i) {
    index[masks[i]] = i;
  }
  return index;
}

const std::vector<uint8_t>& MaskTable() {
  static const std::vector<uint8_t>* table = new std::vector<uint8_t>(BuildBlob47Masks());
  return *table;
}

const std::array<int, 256>& MaskIndex() {
  static const std::array<int, 256>* index = new std::array<int, 256>(BuildMaskIndex(MaskTable()));
  return *index;
}

}  // namespace

uint8_t NormalizeNeighborMask(uint8_t mask) {
  const auto require_edges = [&](uint8_t corner, uint8_t first_edge, uint8_t second_edge) {
    if ((mask & (first_edge | second_edge)) != (first_edge | second_edge)) {
      mask = static_cast<uint8_t>(mask & ~corner);
    }
  };

  require_edges(kNorthEast, kNorth, kEast);
  require_edges(kSouthEast, kEast, kSouth);
  require_edges(kSouthWest, kSouth, kWest);
  require_edges(kNorthWest, kWest, kNorth);
  return mask;
}

absl::Span<const uint8_t> Blob47MaskTable() { return MaskTable(); }

std::optional<int> Blob47IndexForMask(uint8_t normalized_mask) {
  const int index = MaskIndex()[normalized_mask];
  if (index < 0) return std::nullopt;
  return index;
}

QuadrantState QuadrantStateForMask(uint8_t normalized_mask, Quadrant quadrant) {
  const QuadrantBits bits = BitsForQuadrant(quadrant);
  const bool vertical = (normalized_mask & bits.vertical_edge) != 0;
  const bool horizontal = (normalized_mask & bits.horizontal_edge) != 0;

  if (!vertical && !horizontal) return QuadrantState::kOuterCorner;
  if (vertical && !horizontal) return QuadrantState::kEdgeVertical;
  if (!vertical && horizontal) return QuadrantState::kEdgeHorizontal;
  if ((normalized_mask & bits.diagonal) == 0) return QuadrantState::kInnerCorner;
  return QuadrantState::kFill;
}

}  // namespace zebes
