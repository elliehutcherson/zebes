#pragma once

#include "engine/input_types.h"

namespace zebes {

// One fixed simulation tick's player commands. horizontal_axis is -1, 0, or
// 1. jump_pressed is an edge and jump_held is the current level.
struct PlayerInputIntent {
  int horizontal_axis = 0;
  bool jump_pressed = false;
  bool jump_held = false;

  bool operator==(const PlayerInputIntent&) const = default;
};

// Converts platform-neutral held input into deterministic simulation intent.
// The simulation owns previous and advances it after one fixed tick, so a
// render snapshot reused by several catch-up ticks emits the jump edge once.
PlayerInputIntent BuildPlayerInputIntent(const InputSnapshot& current,
                                         const InputSnapshot& previous);

}  // namespace zebes
