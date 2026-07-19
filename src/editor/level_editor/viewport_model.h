#pragma once

#include <cstdint>
#include <map>

#include "absl/status/statusor.h"
#include "objects/blueprint.h"
#include "objects/collider.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/sprite.h"
#include "objects/vec.h"

namespace zebes {

// Opposing corners in level/world coordinates, measured in logical pixels.
struct WorldRect {
  Vec min;
  Vec max;

  constexpr bool IsValid() const { return max.x > min.x && max.y > min.y; }
};

// Identifies one 32x32 TileChunk in Level::tile_chunks. These are chunk-grid
// coordinates, not tile coordinates or world pixels: chunk (1, 0) begins at
// tile (32, 0).
struct TileChunkCoordinate {
  int x = 0;
  int y = 0;

  // Row-major ordering provides deterministic traversal in ordered containers.
  constexpr bool operator<(const TileChunkCoordinate& other) const {
    if (y != other.y) return y < other.y;
    return x < other.x;
  }
};

// Identifies one tile cell in the level-wide grid. Multiplying x and y by the
// level's tile render dimensions produces the cell's world-space origin.
struct TileCoordinate {
  int x = 0;
  int y = 0;
};

// Returns the world-space bounds used consistently for rendering and picking.
// Entities without a usable sprite use a centered 32x32 placeholder.
absl::StatusOr<WorldRect> CalculateEntityBounds(const Entity& entity);

// Returns the active entity whose bounding box contains world_pos, or
// Entity::kInvalidId. Entities without sprites use a 32x32 fallback box.
// TODO: Replace the linear scan with a spatial index when level size requires it.
absl::StatusOr<uint64_t> PickEntity(const std::map<uint64_t, Entity>& entities, Vec world_pos);

// Returns one past the largest existing entity ID, or 1 for an empty level.
uint64_t NextAvailableEntityId(const std::map<uint64_t, Entity>& entities);

// Builds persistent entity state from a blueprint selection. Runtime resource
// pointers remain null until the caller resolves them through Api.
Entity CreateEntityFromBlueprint(const Blueprint& blueprint, int state_index, Vec world_pos,
                                 uint64_t id);

// Encodes chunk coordinates as the key used by Level::tile_chunks.
int64_t ChunkKey(int chunk_x, int chunk_y);

// Decodes the flat level key without exposing bit layout to callers.
TileChunkCoordinate DecodeChunkKey(int64_t key);

// Converts a world position to the containing tile-grid coordinate. Rejects
// invalid cell dimensions, non-finite positions, and coordinates that cannot
// be represented by the level's integer chunk storage.
absl::StatusOr<TileCoordinate> WorldToTileCoordinate(
    Vec world_position, int tile_render_width, int tile_render_height);

// Sets the tile at a world-tile coordinate, creating its chunk if necessary.
// A tile_id of zero erases the tile.
absl::Status SetTileAt(Level& level, int tile_x, int tile_y, int tile_id);

// Returns the tile at a world-tile coordinate, or zero when its chunk is absent.
absl::StatusOr<int> GetTileAt(const Level& level, int tile_x, int tile_y);

// Centers an entity horizontally within the hovered tile and bottom-aligns
// its collider or sprite bounds. Collider geometry takes priority.
absl::StatusOr<Vec> SnapEntityToGrid(Vec mouse_world, int tile_render_w, int tile_render_h,
                                     const Collider* collider, const Sprite* sprite);

}  // namespace zebes
