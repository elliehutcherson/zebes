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

struct WorldRect {
  Vec min;
  Vec max;

  constexpr bool IsValid() const { return max.x > min.x && max.y > min.y; }
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

// Sets the tile at a world-tile coordinate, creating its chunk if necessary.
// A tile_id of zero erases the tile.
void SetTileAt(Level& level, int tile_x, int tile_y, int tile_id);

// Returns the tile at a world-tile coordinate, or zero when its chunk is absent.
int GetTileAt(const Level& level, int tile_x, int tile_y);

// Centers an entity horizontally within the hovered tile and bottom-aligns
// its collider or sprite bounds. Collider geometry takes priority.
absl::StatusOr<Vec> SnapEntityToGrid(Vec mouse_world, int tile_render_w, int tile_render_h,
                                     const Collider* collider, const Sprite* sprite);

}  // namespace zebes
