#pragma once

#include <array>
#include <cstddef>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "engine/tile_collision.h"
#include "objects/level.h"
#include "objects/tileset.h"
#include "objects/vec.h"

namespace zebes {

struct TileCollisionDefinition {
  TileShape shape = TileShape::kNone;
  bool is_one_way = false;
};

using TileCollisionLookup = absl::flat_hash_map<int, TileCollisionDefinition>;

// Builds the immutable, compact collision view used by fixed ticks. This is a
// boot operation; movement never searches the Tileset definition vector.
absl::StatusOr<TileCollisionLookup> BuildTileCollisionLookup(const Tileset& tileset);

struct TileMovementContact {
  int tile_x = 0;
  int tile_y = 0;
  int tile_id = 0;
  double time = 0.0;
  Vec normal;
};

inline constexpr size_t kMaxTileMovementContactCount = 16;

struct TileMovementResult {
  AxisAlignedBox box;
  Vec velocity;
  std::array<TileMovementContact, kMaxTileMovementContactCount> contacts;
  size_t contact_count = 0;
  bool grounded = false;
};

struct TileMovementOptions {
  const WorldLayer& layer;
  const TileCollisionLookup& tiles;
  int tile_width = 0;
  int tile_height = 0;
  AxisAlignedBox box;
  Vec displacement;
  Vec velocity;
};

// Moves one AABB through the occupied cells in a sparse tile layer. Only cells
// intersecting the current swept bounds are queried, in row-major coordinate
// order. The solver repeatedly resolves the earliest simultaneous contacts,
// projects displacement and velocity along their blocking normals, and fails
// rather than returning a partial result if bounded response cannot converge.
// Level edges are not implicit colliders; authored tile geometry is the only
// static collision authority.
absl::StatusOr<TileMovementResult> MoveBoxThroughTileLayer(const TileMovementOptions& options);

}  // namespace zebes
