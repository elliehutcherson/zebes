#pragma once

#include "vec.h"

namespace zebes {

struct Transform {
  Vec position;
  float rotation = 0;

  bool operator==(const Transform& other) const = default;
};

}  // namespace zebes