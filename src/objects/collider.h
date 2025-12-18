#pragma once

#include <vector>

#include "vec.h"

namespace zebes {

using Polygon = std::vector<Vec>;

struct Collider {
  std::string id;
  // Points are offset from the transform position. For SAT (Separating Axis Theorem) collision, the
  // requirement is that the polygon is Convex (no internal angles > 180°).
  std::vector<Polygon> polygons;
};

}  // namespace zebes