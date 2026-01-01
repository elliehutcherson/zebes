#pragma once

#include "vec.h"

namespace zebes {

struct Body {
  Vec velocity;
  Vec acceleration;
  Vec drag;
  double mass = 0;
  bool is_static = false;
};

}  // namespace zebes