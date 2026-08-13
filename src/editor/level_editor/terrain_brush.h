#pragma once

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/level.h"
#include "objects/tileset.h"

namespace zebes {

// Reverse lookup from tile ID to the terrain that owns it.
//
// The Level Editor rebuilds this each frame from Api-owned tileset storage, so
// the referenced tileset must outlive the index. Tiles absent from the index
// belong to no terrain and are treated as foreign material by the brush.
class TerrainIndex {
 public:
  // Fails when a tile belongs to more than one terrain, which would make a
  // painted cell's neighbourhood ambiguous.
  static absl::StatusOr<TerrainIndex> Build(const Tileset& tileset);

  // Returns the terrain owning tile_id, counting both painted tiles and
  // hand-placed members such as slopes. Use this when deciding whether a
  // neighbour is the same material. Tile ID 0 is always null.
  const Terrain* FindByTileId(int tile_id) const;

  // Returns the terrain only when the brush itself produces this tile.
  //
  // The distinction matters: a member tile counts toward a neighbour's mask but
  // must never be re-resolved, or refreshing around it would overwrite a
  // hand-placed slope with a blob tile.
  const Terrain* FindPaintableByTileId(int tile_id) const;

  const Terrain* FindById(int terrain_id) const;

 private:
  // What a tile ID means to the terrain that claims it.
  struct TileOwnership {
    const Terrain* terrain = nullptr;
    // False for member-only tiles, which the brush reads but never writes.
    bool paintable = false;
  };

  // Records every tile a terrain paints or counts, rejecting tiles claimed
  // twice or listed as both paintable and member.
  absl::Status IndexTerrainTiles(const Terrain& terrain);
  absl::Status ClaimTile(int tile_id, const Terrain& terrain, bool paintable);

  absl::flat_hash_map<int, TileOwnership> tile_ownership_;
  absl::flat_hash_map<int, const Terrain*> terrain_by_id_;
};

// Returns the normalized neighbour mask for a cell painted with terrain.
//
// A neighbour contributes its bit only when it holds a tile of the same
// terrain. Coordinates outside the level follow Terrain::solid_outside_level,
// which is what keeps ground continuous at the world border.
absl::StatusOr<uint8_t> ComputeTerrainMask(const Level& level, const TerrainIndex& index,
                                           const Terrain& terrain, int tile_x, int tile_y);

// Picks a tile for a rule deterministically from the cell's coordinates.
//
// The same cell always yields the same tile, so repainting a region never
// reshuffles its artwork and tests observe stable results.
//
// Terrain::variant_period decides how: zero picks by weight from a hash of the
// coordinates, and a positive period lays the variants down as fixed phases of
// one repeating pattern instead.
absl::StatusOr<int> SelectVariant(const Terrain& terrain, const TerrainRule& rule, int tile_x,
                                  int tile_y);

// Recomputes the artwork for a cell already owned by terrain, without changing
// which terrain occupies it. Exposed for tests and for bulk refresh after a
// tileset edit.
absl::Status ResolveTerrainCell(Level& level, const TerrainIndex& index, const Terrain& terrain,
                                int tile_x, int tile_y);

// Writes terrain_id at (tile_x, tile_y) and re-resolves the neighbouring cells
// of the same terrain so their edges and corners stay consistent.
absl::Status PaintTerrain(Level& level, const TerrainIndex& index, int terrain_id, int tile_x,
                          int tile_y);

// Clears the cell and re-resolves the neighbours that belonged to whatever
// terrain occupied it. Erasing a cell holding no terrain still clears it.
absl::Status EraseTerrain(Level& level, const TerrainIndex& index, int tile_x, int tile_y);

}  // namespace zebes
