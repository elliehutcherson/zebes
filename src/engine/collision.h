#pragma once

#include <optional>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "objects/vec.h"

namespace zebes {

// General world-space collision geometry. Tile collision adapts authored tile
// shapes to these types; entity and projectile collision can use them directly.
struct AxisAlignedBox {
  Vec min;
  Vec max;

  bool operator==(const AxisAlignedBox&) const = default;
};

struct CollisionContact {
  Vec normal;
  double penetration = 0.0;

  bool operator==(const CollisionContact&) const = default;
};

struct CollisionSweepContact {
  double time = 0.0;
  Vec normal;

  bool operator==(const CollisionSweepContact&) const = default;
};

AxisAlignedBox TranslateBox(AxisAlignedBox box, Vec displacement);

// Removes the portion of vector directed into a surface. outward_normal must
// be normalized and point away from the blocking geometry.
Vec RemoveInwardComponent(Vec vector, Vec outward_normal);

// Tests an AABB against one convex world-space polygon. Touching without
// positive overlap is not a contact. Invalid, non-finite, degenerate, or
// concave polygons fail rather than producing undefined response.
absl::StatusOr<std::optional<CollisionContact>> IntersectBoxWithConvexPolygon(
    AxisAlignedBox box, absl::Span<const Vec> polygon);

// Sweeps an AABB by displacement against one convex world-space polygon over
// normalized time [0, 1]. Initial positive overlap reports time zero; grazing,
// tangential movement, and movement away from touching geometry do not hit.
absl::StatusOr<std::optional<CollisionSweepContact>> SweepBoxWithConvexPolygon(
    AxisAlignedBox box, Vec displacement, absl::Span<const Vec> polygon);

}  // namespace zebes
