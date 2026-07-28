#pragma once

#include "vec.h"

namespace zebes {

// Authored physical properties of an entity. This is design-time data: it is
// serialized with the level and does not change while the game runs.
struct Body {
  // Resistance applied per axis. Zero means no damping.
  Vec drag;

  double mass = 0;

  // Static bodies are never integrated and never moved by collision response.
  bool is_static = false;
};

// Per-frame simulation state. Deliberately not part of Body and never
// serialized: velocity and acceleration are outputs of the running game, not
// authored level content. Saving a level must not capture how fast something
// happened to be moving at the moment of the save.
struct Motion {
  Vec velocity;
  Vec acceleration;
};

}  // namespace zebes
