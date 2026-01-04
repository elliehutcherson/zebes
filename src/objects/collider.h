#pragma once

#include <vector>

#include "absl/strings/str_cat.h"
#include "vec.h"

namespace zebes {

using Polygon = std::vector<Vec>;

struct Collider {
  std::string id;
  std::string name;
  // Points are offset from the transform position. For SAT (Separating Axis Theorem) collision, the
  // requirement is that the polygon is Convex (no internal angles > 180°).
  std::vector<Polygon> polygons;

  std::string name_id() const { return absl::StrCat(name, ",", id); }
};

}  // namespace zebes