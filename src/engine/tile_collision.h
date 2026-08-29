#pragma once

#include <optional>

#include "absl/status/statusor.h"
#include "objects/tileset.h"
#include "objects/vec.h"

namespace zebes {

// An axis-aligned box in world-space logical pixels. Both axes use the engine's
// screen-space convention: x grows right and y grows down.
struct AxisAlignedBox {
  Vec min;
  Vec max;

  bool operator==(const AxisAlignedBox&) const = default;
};

// The minimum translation that separates an overlapping box from a tile.
// Applying normal * penetration to the box resolves this one static overlap.
// Touching without positive overlap produces no contact.
struct TileCollisionContact {
  Vec normal;
  double penetration = 0.0;

  bool operator==(const TileCollisionContact&) const = default;
};

// Tests one world-space box against the convex polygon defined by shape. Tile
// geometry is read only from TileShapePolygon, then scaled to tile_size and
// translated by tile_origin. kNone returns no contact.
//
// This is a static overlap primitive, not a movement solver: callers remain
// responsible for broad-phase tile selection, one-way policy, sweep ordering,
// and choosing how velocity changes after resolution. Invalid or non-finite
// geometry is rejected instead of being interpreted as an empty collision.
absl::StatusOr<std::optional<TileCollisionContact>> IntersectBoxWithTileShape(AxisAlignedBox box,
                                                                              TileShape shape,
                                                                              Vec tile_origin,
                                                                              Vec tile_size);

}  // namespace zebes
