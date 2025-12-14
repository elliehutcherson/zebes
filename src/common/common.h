#pragma once

#include <algorithm>

#include "common/config.h"
#include "common/vector.h"

namespace zebes {

struct RenderData {
  const std::vector<Point> vertices;
  bool static_position = true;
};

constexpr int kMaxTextureNameLength = 64;

}  // namespace zebes