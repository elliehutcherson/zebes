#pragma once

#include <algorithm>

#include "common/vector.h"
#include "common/config.h"

namespace zebes {

struct RenderData {
  const std::vector<Point> vertices;
  bool static_position = true;
};

inline Point GetWorldPosition(const Point &window_position,
                              const Point &world_center,
                              const Point &window_dimensions,
                              const BoundaryConfig &boundaries) {
  Point world_justified = {.x = world_center.x - window_dimensions.x / 2,
                           .y = world_center.y - window_dimensions.y / 2};

  world_justified.x =
      std::max(static_cast<double>(boundaries.x_min), world_justified.x);
  world_justified.x =
      std::min(static_cast<double>(boundaries.x_max), world_justified.x);
  world_justified.y =
      std::max(static_cast<double>(boundaries.y_min), world_justified.y);
  world_justified.y =
      std::min(static_cast<double>(boundaries.y_max), world_justified.y);

  return Point{.x = window_position.x + world_justified.x,
               .y = window_position.y + world_justified.y};
}

inline Point GetWorldTile(const Point &world_position,
                          const Point &tile_dimensions) {
  return Point{.x = world_position.x / tile_dimensions.x,
               .y = world_position.y / tile_dimensions.y};
}



} // namespace zebes