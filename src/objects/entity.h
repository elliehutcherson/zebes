#pragma once

#include <cstdint>

#include "objects/body.h"
#include "objects/collider.h"
#include "objects/sprite.h"
#include "objects/transform.h"

namespace zebes {

struct Entity {
  static constexpr uint64_t kInvalidId = 0;

  uint64_t id = kInvalidId;  // Unique Runtime ID (for safe lookups)
  bool active = true;        // For "soft" deletion

  // MUTABLE STATE (Owned by Entity)
  // Every entity needs a distinct position, so this is stored by Value.
  Transform transform;
  Body body;  // Physics velocity/acceleration

  // IMMUTABLE ASSETS (Owned by Managers)
  // These are raw pointers because Entity does NOT own them.
  // The Managers guarantee these pointers remain valid.
  const Sprite* sprite = nullptr;
  const Collider* collider = nullptr;

  // If you need unique instance data for a component (e.g. current animation frame),
  // you store that small state here, separate from the heavy Sprite asset.
  int current_frame_index = 0;
  double animation_timer = 0.0;
};

}  // namespace zebes