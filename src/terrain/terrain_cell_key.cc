#include "terrain/terrain_cell_key.h"

#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace zebes {

uint8_t NeighborMaskOf(const TerrainCellKey& key) {
  uint8_t mask = 0;
  for (int i = 0; i < kNeighborCount; ++i) {
    if (key.neighbors[i] != TileShape::kNone) mask |= static_cast<uint8_t>(1 << i);
  }
  return mask;
}

std::string DebugString(const TerrainCellKey& key) {
  std::vector<std::string> neighbors;
  neighbors.reserve(kNeighborCount);
  for (const TileShape neighbor : key.neighbors) {
    neighbors.push_back(kTileShapeIdentifiers[static_cast<size_t>(neighbor)]);
  }
  return absl::StrCat(kTileShapeIdentifiers[static_cast<size_t>(key.shape)], " phase ", key.phase,
                      " [", absl::StrJoin(neighbors, " "), "]");
}

}  // namespace zebes
