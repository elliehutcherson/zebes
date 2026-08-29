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

// The first contact while an axis-aligned box translates by displacement over
// the normalized interval [0, 1]. `time` is the time of impact and `normal`
// points out of the tile and into the box. A contact is reported at t=0 for an
// initial positive overlap (using the static overlap normal). Initial
// touching is not an overlap: it is reported only when the displacement moves
// into the tile. Moving away from a touching tile, tangential motion, and
// zero displacement report no contact. Exact grazing (zero-duration overlap)
// is not a collision.
struct TileCollisionSweepContact {
  double time = 0.0;
  Vec normal;

  bool operator==(const TileCollisionSweepContact&) const = default;
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

// Sweeps box from its current position by displacement against one convex
// TileShapePolygon. This is a narrow-phase query only; it does not select
// neighboring tiles or resolve velocity. The dynamic separating-axis test
// rejects invalid, non-finite, and degenerate geometry with an error. kNone
// returns no contact.
absl::StatusOr<std::optional<TileCollisionSweepContact>> SweepBoxWithTileShape(
    AxisAlignedBox box, Vec displacement, TileShape shape, Vec tile_origin, Vec tile_size);

}  // namespace zebes
