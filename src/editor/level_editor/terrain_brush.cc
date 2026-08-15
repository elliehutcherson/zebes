#include "editor/level_editor/terrain_brush.h"

#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/level_editor/viewport_model.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

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
// Every owned neighbour is refreshed, including hand-placed pieces such as
// slopes. That is safe because each is handed back the shape it already has, so
// re-resolving one can change how it looks but never what it is.
absl::Status RefreshNeighbors(Level& level, TerrainIndex& index, const Terrain& terrain,
                              TerrainTileProvider& provider, int tile_x, int tile_y) {
  for (const NeighborOffset& offset : kNeighborOffsets) {
    const int x = tile_x + offset.dx;
    const int y = tile_y + offset.dy;
    if (IsOutsideLevel(level, x, y)) continue;

    ASSIGN_OR_RETURN(const int neighbor_tile, GetTileAt(level, x, y));
    if (index.FindByTileId(neighbor_tile) != &terrain) continue;

    // The neighbour is handed back the geometry it already has. A refresh may
    // change how a cell looks; it must never change what the player collides
    // with, and passing the shape we just read is what makes that structural
    // rather than a rule to remember.
    RETURN_IF_ERROR(ResolveTerrainCell(level, index, terrain, provider,
                                       index.ShapeOfTile(neighbor_tile), x, y));
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

    RETURN_IF_ERROR(index.IndexTerrainTiles(terrain, tileset));
  }
  return index;
}

absl::Status TerrainIndex::ClaimTile(int tile_id, const Terrain& terrain,
                                     const Tileset& tileset) {
  TileShape shape = TileShape::kNone;
  for (const Tile& tile : tileset.tiles) {
    if (tile.id == tile_id) {
      shape = tile.shape;
      break;
    }
  }

  auto [entry, inserted] =
      tile_ownership_.emplace(tile_id, TileOwnership{.terrain = &terrain, .shape = shape});
  if (inserted) return absl::OkStatus();

  if (entry->second.terrain != &terrain) {
    return absl::InvalidArgumentError(
        absl::StrCat("tile ", tile_id, " belongs to both terrain '", entry->second.terrain->name,
                     "' and '", terrain.name, "'"));
  }
  return absl::OkStatus();
}

absl::Status TerrainIndex::IndexTerrainTiles(const Terrain& terrain, const Tileset& tileset) {
  for (const TerrainRule& rule : terrain.rules) {
    for (const TerrainVariant& variant : rule.variants) {
      RETURN_IF_ERROR(ClaimTile(variant.tile_id, terrain, tileset));
    }
  }
  // A derived terrain's tiles carry the neighbourhood they depict; the shape is
  // in the key rather than looked up, because many of its tiles share one shape
  // on purpose and differ only by what is beside them.
  for (const DerivedTile& derived : terrain.derived_tiles) {
    RETURN_IF_ERROR(ClaimTile(derived.tile_id, terrain, tileset));
  }

  for (int tile_id : terrain.shape_tile_ids) {
    RETURN_IF_ERROR(ClaimTile(tile_id, terrain, tileset));

    // Two tiles for one shape would leave which of them a cell gets to the
    // order of the list, so the first wins and the second is refused.
    const TileShape shape = ShapeOfTile(tile_id);
    auto [entry, inserted] = shape_tiles_.emplace(std::make_pair(terrain.id, shape), tile_id);
    if (!inserted && entry->second != tile_id) {
      return absl::InvalidArgumentError(absl::StrCat(
          "terrain '", terrain.name, "' declares tiles ", entry->second, " and ", tile_id,
          " for the same shape ", kTileShapeIdentifiers[static_cast<size_t>(shape)]));
    }
  }
  return absl::OkStatus();
}

const Terrain* TerrainIndex::FindByTileId(int tile_id) const {
  auto found = tile_ownership_.find(tile_id);
  if (found == tile_ownership_.end()) return nullptr;
  return found->second.terrain;
}

const Terrain* TerrainIndex::FindById(int terrain_id) const {
  auto found = terrain_by_id_.find(terrain_id);
  if (found == terrain_by_id_.end()) return nullptr;
  return found->second;
}

TileShape TerrainIndex::ShapeOfTile(int tile_id) const {
  auto found = tile_ownership_.find(tile_id);
  if (found == tile_ownership_.end()) return TileShape::kNone;
  return found->second.shape;
}

absl::Status TerrainIndex::NoteResolvedTile(int tile_id, const Terrain& terrain, TileShape shape) {
  auto [entry, inserted] =
      tile_ownership_.emplace(tile_id, TileOwnership{.terrain = &terrain, .shape = shape});
  if (inserted) return absl::OkStatus();

  if (entry->second.terrain != &terrain) {
    return absl::InvalidArgumentError(
        absl::StrCat("tile ", tile_id, " was resolved for terrain '", terrain.name,
                     "' but belongs to '", entry->second.terrain->name, "'"));
  }
  return absl::OkStatus();
}

std::optional<int> TerrainIndex::FindShapeTile(const Terrain& terrain, TileShape shape) const {
  auto found = shape_tiles_.find(std::make_pair(terrain.id, shape));
  if (found == shape_tiles_.end()) return std::nullopt;
  return found->second;
}

absl::StatusOr<TerrainCellKey> ComputeTerrainCellKey(const Level& level, const TerrainIndex& index,
                                                     const Terrain& terrain, TileShape shape,
                                                     int tile_x, int tile_y) {
  RETURN_IF_ERROR(ValidateCell(level, tile_x, tile_y));

  TerrainCellKey key;
  key.shape = shape;
  for (int i = 0; i < kNeighborCount; ++i) {
    const int x = tile_x + kNeighborOffsets[i].dx;
    const int y = tile_y + kNeighborOffsets[i].dy;

    if (IsOutsideLevel(level, x, y)) {
      // Outside the level reads as solid ground of this terrain, which is what
      // stops a coastline being drawn along the world border.
      key.neighbors[i] = terrain.solid_outside_level ? TileShape::kFullBlock : TileShape::kNone;
      continue;
    }

    ASSIGN_OR_RETURN(const int neighbor_tile, GetTileAt(level, x, y));
    key.neighbors[i] = index.FindByTileId(neighbor_tile) == &terrain
                           ? index.ShapeOfTile(neighbor_tile)
                           : TileShape::kNone;
  }

  // A periodic terrain's artwork is one pattern laid down in phases, so which
  // phase a cell shows is fixed by where the cell sits rather than chosen.
  const int period = terrain.variant_period;
  if (period > 0) {
    const int phase_x = ((tile_x % period) + period) % period;
    const int phase_y = ((tile_y % period) + period) % period;
    key.phase = phase_y * period + phase_x;
  }
  return key;
}

absl::StatusOr<uint8_t> ComputeTerrainMask(const Level& level, const TerrainIndex& index,
                                           const Terrain& terrain, int tile_x, int tile_y) {
  ASSIGN_OR_RETURN(
      const TerrainCellKey key,
      ComputeTerrainCellKey(level, index, terrain, TileShape::kFullBlock, tile_x, tile_y));
  return NormalizeNeighborMask(NeighborMaskOf(key));
}

absl::StatusOr<int> Blob47TileProvider::TileForKey(const Terrain& terrain,
                                                   const TerrainCellKey& key, int tile_x,
                                                   int tile_y) {
  if (key.shape != TileShape::kFullBlock) {
    const std::optional<int> tile = index_.FindShapeTile(terrain, key.shape);
    if (!tile.has_value()) {
      return absl::NotFoundError(
          absl::StrCat("terrain '", terrain.name, "' has no artwork for shape ",
                       kTileShapeIdentifiers[static_cast<size_t>(key.shape)]));
    }
    return *tile;
  }

  const uint8_t mask = NormalizeNeighborMask(NeighborMaskOf(key));
  const TerrainRule* rule = FindRule(terrain, mask);
  if (rule == nullptr) {
    return absl::NotFoundError(absl::StrCat("terrain '", terrain.name, "' has no rule for mask ",
                                            static_cast<int>(mask)));
  }
  return SelectVariant(terrain, *rule, tile_x, tile_y);
}

absl::StatusOr<int> SelectVariant(const Terrain& terrain, const TerrainRule& rule, int tile_x,
                                  int tile_y) {
  if (rule.variants.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("terrain rule for mask ", static_cast<int>(rule.mask), " has no variants"));
  }

  // A periodic terrain's variants are phases of one pattern, so which one a
  // cell gets is fixed by where the cell sits in the period. Picking by weight
  // here would tear the pattern at every tile border.
  if (terrain.variant_period > 0) {
    const int period = terrain.variant_period;
    if (static_cast<int>(rule.variants.size()) != period * period) {
      return absl::InvalidArgumentError(absl::StrCat(
          "terrain '", terrain.name, "' repeats every ", period, " tiles and so needs ",
          period * period, " variants for mask ", static_cast<int>(rule.mask), ", but has ",
          rule.variants.size()));
    }
    const int phase_x = ((tile_x % period) + period) % period;
    const int phase_y = ((tile_y % period) + period) % period;
    return rule.variants[phase_y * period + phase_x].tile_id;
  }

  int total_weight = 0;
  for (const TerrainVariant& variant : rule.variants) {
    if (variant.weight <= 0) {
      return absl::InvalidArgumentError("terrain variant weights must be positive");
    }
    total_weight += variant.weight;
  }

  int remaining = static_cast<int>(HashCell(tile_x, tile_y, terrain.id) % total_weight);
  for (const TerrainVariant& variant : rule.variants) {
    remaining -= variant.weight;
    if (remaining < 0) return variant.tile_id;
  }
  return rule.variants.back().tile_id;
}

absl::Status ResolveTerrainCell(Level& level, TerrainIndex& index, const Terrain& terrain,
                                TerrainTileProvider& provider, TileShape shape, int tile_x,
                                int tile_y) {
  ASSIGN_OR_RETURN(const TerrainCellKey key,
                   ComputeTerrainCellKey(level, index, terrain, shape, tile_x, tile_y));
  ASSIGN_OR_RETURN(const int tile_id, provider.TileForKey(terrain, key, tile_x, tile_y));
  // A derived provider may have invented this tile just now, and the cells
  // around it are about to be resolved against what this one holds.
  RETURN_IF_ERROR(index.NoteResolvedTile(tile_id, terrain, shape));
  return SetTileAt(level, tile_x, tile_y, tile_id);
}

absl::Status PaintTerrain(Level& level, TerrainIndex& index, TerrainTileProvider& provider,
                          int terrain_id, TileShape shape, int tile_x, int tile_y) {
  RETURN_IF_ERROR(ValidateCell(level, tile_x, tile_y));

  const Terrain* terrain = index.FindById(terrain_id);
  if (terrain == nullptr) {
    return absl::NotFoundError(absl::StrCat("unknown terrain ID ", terrain_id));
  }

  // A cell's key is computed from its neighbours only, so resolving the centre
  // first both claims it and gives it correct artwork. The neighbours then see
  // it as occupied and re-resolve against it.
  RETURN_IF_ERROR(ResolveTerrainCell(level, index, *terrain, provider, shape, tile_x, tile_y));
  return RefreshNeighbors(level, index, *terrain, provider, tile_x, tile_y);
}

absl::Status EraseTerrain(Level& level, TerrainIndex& index, TerrainTileProvider& provider,
                          int tile_x, int tile_y) {
  RETURN_IF_ERROR(ValidateCell(level, tile_x, tile_y));

  ASSIGN_OR_RETURN(const int existing, GetTileAt(level, tile_x, tile_y));
  const Terrain* terrain = index.FindByTileId(existing);
  RETURN_IF_ERROR(SetTileAt(level, tile_x, tile_y, 0));

  if (terrain == nullptr) return absl::OkStatus();
  return RefreshNeighbors(level, index, *terrain, provider, tile_x, tile_y);
}

}  // namespace zebes
