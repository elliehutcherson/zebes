#include "editor/level_editor/level_tiles.h"

namespace zebes {

bool LevelHasTiles(const Level& level) {
  for (const WorldLayer& layer : level.layers) {
    for (const auto& entry : layer.tile_chunks) {
      for (const int tile_id : entry.second.tiles) {
        if (tile_id != 0) return true;
      }
    }
  }
  return false;
}

int CountPlacedTiles(const Level& level) {
  int count = 0;
  for (const WorldLayer& layer : level.layers) {
    for (const auto& entry : layer.tile_chunks) {
      for (const int tile_id : entry.second.tiles) {
        if (tile_id != 0) ++count;
      }
    }
  }
  return count;
}

}  // namespace zebes
