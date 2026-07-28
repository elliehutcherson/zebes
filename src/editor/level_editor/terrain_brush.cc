#include "editor/level_editor/terrain_brush.h"

#include <array>

#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/level_editor/viewport_model.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

// One neighbour direction and the mask bit it contributes.
struct NeighborOffset {
  int dx;
  int dy;
  uint8_t bit;
};

// Screen-space directions: negative y is north.
constexpr std::array<NeighborOffset, 8> kNeighbors = {{
    {.dx = 0, .dy = -1, .bit = kNorth},
    {.dx = 1, .dy = -1, .bit = kNorthEast},
    {.dx = 1, .dy = 0, .bit = kEast},
    {.dx = 1, .dy = 1, .bit = kSouthEast},
    {.dx = 0, .dy = 1, .bit = kSouth},
    {.dx = -1, .dy = 1, .bit = kSouthWest},
    {.dx = -1, .dy = 0, .bit = kWest},
    {.dx = -1, .dy = -1, .bit = kNorthWest},
}};

// Returns whether a tile coordinate falls outside the level's authored bounds.
// Cells inside the level but never painted are empty, not outside.
bool IsOutsideLevel(const Level& level, int tile_x, int tile_y) {
  if (tile_x < 0 || tile_y < 0) return true;

  const int tiles_wide = static_cast<int>(level.width) / level.tile_render_width;
  const int tiles_high = static_cast<int>(level.height) / level.tile_render_height;
  return tile_x >= tiles_wide || tile_y >= tiles_high;
}

// Mixes a cell coordinate into a well-distributed value so variant choice looks
// random but is fully reproducible.
uint64_t HashCell(int tile_x, int tile_y, int terrain_id) {
  uint64_t hash = static_cast<uint64_t>(static_cast<uint32_t>(tile_x)) * 0x9E3779B97F4A7C15ull;
  hash ^= static_cast<uint64_t>(static_cast<uint32_t>(tile_y)) * 0xC2B2AE3D27D4EB4Full;
  hash ^= static_cast<uint64_t>(static_cast<uint32_t>(terrain_id)) * 0x165667B19E3779F9ull;
  hash ^= hash >> 33;
  hash *= 0xFF51AFD7ED558CCDull;
  hash ^= hash >> 33;
  return hash;
}

const TerrainRule* FindRule(const Terrain& terrain, uint8_t mask) {
  for (const TerrainRule& rule : terrain.rules) {
    if (rule.mask == mask) return &rule;
  }
  return nullptr;
}

absl::Status ValidateCell(const Level& level, int tile_x, int tile_y) {
  if (level.tile_render_width <= 0 || level.tile_render_height <= 0) {
    return absl::InvalidArgumentError("tile render dimensions must be positive");
  }
  if (tile_x < 0 || tile_y < 0) {
    return absl::InvalidArgumentError("tile coordinates must be non-negative");
  }
  return absl::OkStatus();
}

// Re-resolves the eight cells around a coordinate that the brush owns. The
// centre is deliberately excluded so callers control it explicitly.
//
// Only paintable cells are refreshed. A neighbouring member tile — a slope, say
// — counts toward masks but is never rewritten, because re-resolving it would
// replace hand-placed artwork with a blob tile.
absl::Status RefreshNeighbors(Level& level, const TerrainIndex& index, const Terrain& terrain,
                              int tile_x, int tile_y) {
  for (const NeighborOffset& offset : kNeighbors) {
    const int x = tile_x + offset.dx;
    const int y = tile_y + offset.dy;
    if (IsOutsideLevel(level, x, y)) continue;

    ASSIGN_OR_RETURN(const int neighbor_tile, GetTileAt(level, x, y));
    if (index.FindPaintableByTileId(neighbor_tile) != &terrain) continue;
    RETURN_IF_ERROR(ResolveTerrainCell(level, index, terrain, x, y));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<TerrainIndex> TerrainIndex::Build(const Tileset& tileset) {
  TerrainIndex index;
  for (const Terrain& terrain : tileset.terrains) {
    if (!index.terrain_by_id_.emplace(terrain.id, &terrain).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("duplicate terrain ID ", terrain.id, " in tileset '", tileset.name, "'"));
    }

    RETURN_IF_ERROR(index.IndexTerrainTiles(terrain));
  }
  return index;
}

absl::Status TerrainIndex::ClaimTile(int tile_id, const Terrain& terrain, bool paintable) {
  auto [entry, inserted] = tile_ownership_.emplace(
      tile_id, TileOwnership{.terrain = &terrain, .paintable = paintable});
  if (inserted) return absl::OkStatus();

  if (entry->second.terrain != &terrain) {
    return absl::InvalidArgumentError(
        absl::StrCat("tile ", tile_id, " belongs to both terrain '", entry->second.terrain->name,
                     "' and '", terrain.name, "'"));
  }
  if (entry->second.paintable != paintable) {
    return absl::InvalidArgumentError(
        absl::StrCat("tile ", tile_id, " is both painted and a member of terrain '", terrain.name,
                     "'"));
  }
  return absl::OkStatus();
}

absl::Status TerrainIndex::IndexTerrainTiles(const Terrain& terrain) {
  for (const TerrainRule& rule : terrain.rules) {
    for (const TerrainVariant& variant : rule.variants) {
      RETURN_IF_ERROR(ClaimTile(variant.tile_id, terrain, /*paintable=*/true));
    }
  }
  for (int tile_id : terrain.member_tile_ids) {
    RETURN_IF_ERROR(ClaimTile(tile_id, terrain, /*paintable=*/false));
  }
  return absl::OkStatus();
}

const Terrain* TerrainIndex::FindByTileId(int tile_id) const {
  auto found = tile_ownership_.find(tile_id);
  if (found == tile_ownership_.end()) return nullptr;
  return found->second.terrain;
}

const Terrain* TerrainIndex::FindPaintableByTileId(int tile_id) const {
  auto found = tile_ownership_.find(tile_id);
  if (found == tile_ownership_.end() || !found->second.paintable) return nullptr;
  return found->second.terrain;
}

const Terrain* TerrainIndex::FindById(int terrain_id) const {
  auto found = terrain_by_id_.find(terrain_id);
  if (found == terrain_by_id_.end()) return nullptr;
  return found->second;
}

absl::StatusOr<uint8_t> ComputeTerrainMask(const Level& level, const TerrainIndex& index,
                                           const Terrain& terrain, int tile_x, int tile_y) {
  RETURN_IF_ERROR(ValidateCell(level, tile_x, tile_y));

  uint8_t mask = 0;
  for (const NeighborOffset& offset : kNeighbors) {
    const int x = tile_x + offset.dx;
    const int y = tile_y + offset.dy;

    if (IsOutsideLevel(level, x, y)) {
      if (terrain.solid_outside_level) mask |= offset.bit;
      continue;
    }

    ASSIGN_OR_RETURN(const int neighbor_tile, GetTileAt(level, x, y));
    if (index.FindByTileId(neighbor_tile) == &terrain) mask |= offset.bit;
  }

  return NormalizeNeighborMask(mask);
}

absl::StatusOr<int> SelectVariant(const TerrainRule& rule, int tile_x, int tile_y, int terrain_id) {
  if (rule.variants.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("terrain rule for mask ", static_cast<int>(rule.mask), " has no variants"));
  }

  int total_weight = 0;
  for (const TerrainVariant& variant : rule.variants) {
    if (variant.weight <= 0) {
      return absl::InvalidArgumentError("terrain variant weights must be positive");
    }
    total_weight += variant.weight;
  }

  int remaining = static_cast<int>(HashCell(tile_x, tile_y, terrain_id) % total_weight);
  for (const TerrainVariant& variant : rule.variants) {
    remaining -= variant.weight;
    if (remaining < 0) return variant.tile_id;
  }
  return rule.variants.back().tile_id;
}

absl::Status ResolveTerrainCell(Level& level, const TerrainIndex& index, const Terrain& terrain,
                                int tile_x, int tile_y) {
  ASSIGN_OR_RETURN(const uint8_t mask, ComputeTerrainMask(level, index, terrain, tile_x, tile_y));

  const TerrainRule* rule = FindRule(terrain, mask);
  if (rule == nullptr) {
    return absl::NotFoundError(absl::StrCat("terrain '", terrain.name, "' has no rule for mask ",
                                            static_cast<int>(mask)));
  }

  ASSIGN_OR_RETURN(const int tile_id, SelectVariant(*rule, tile_x, tile_y, terrain.id));
  return SetTileAt(level, tile_x, tile_y, tile_id);
}

absl::Status PaintTerrain(Level& level, const TerrainIndex& index, int terrain_id, int tile_x,
                          int tile_y) {
  RETURN_IF_ERROR(ValidateCell(level, tile_x, tile_y));

  const Terrain* terrain = index.FindById(terrain_id);
  if (terrain == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown terrain ID ", terrain_id));
  }

  // A cell's mask is computed from its neighbours only, so resolving the centre
  // first both claims it and gives it correct artwork. The neighbours then see
  // it as occupied and re-resolve against it.
  RETURN_IF_ERROR(ResolveTerrainCell(level, index, *terrain, tile_x, tile_y));
  return RefreshNeighbors(level, index, *terrain, tile_x, tile_y);
}

absl::Status EraseTerrain(Level& level, const TerrainIndex& index, int tile_x, int tile_y) {
  RETURN_IF_ERROR(ValidateCell(level, tile_x, tile_y));

  ASSIGN_OR_RETURN(const int existing, GetTileAt(level, tile_x, tile_y));
  const Terrain* terrain = index.FindByTileId(existing);
  RETURN_IF_ERROR(SetTileAt(level, tile_x, tile_y, 0));

  if (terrain == nullptr) return absl::OkStatus();
  return RefreshNeighbors(level, index, *terrain, tile_x, tile_y);
}

}  // namespace zebes
