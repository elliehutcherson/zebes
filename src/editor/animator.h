#pragma once

#include "engine/animation.h"

namespace zebes {

// Compatibility name for editor callers. Playback behavior is implemented by
// the engine-owned cursor so runtime and editor cannot drift apart.
using Animator = AnimationCursor;

}  // namespace zebes
