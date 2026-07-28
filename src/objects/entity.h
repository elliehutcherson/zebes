#pragma once

#include <cstdint>
#include <string>

#include "objects/body.h"
#include "objects/transform.h"

namespace zebes {

// A placed entity as authored. Deliberately free of resolved asset pointers:
// assets are referenced by ID so this stays a pure, serializable definition and
// loading a level does not require the sprite or collider managers.
struct Entity {
  static constexpr uint64_t kInvalidId = 0;

  uint64_t id = kInvalidId;  // Unique Runtime ID (for safe lookups)
  bool active = true;        // For "soft" deletion

  // AUTHORED STATE (Owned by Entity)
  // Every entity needs a distinct position, so this is stored by Value.
  Transform transform;
  // Authored physical properties only. Velocity and acceleration are runtime
  // simulation state and live in Motion.
  Body body;

  // BLUEPRINT REFERENCE (for serialization and editor display)
  // Identifies which blueprint and state this entity was spawned from.
  std::string blueprint_id;
  int blueprint_state_index = 0;

  // ASSET REFERENCES (Owned by Managers)
  // Stored by ID rather than by pointer. Rendering and picking resolve these
  // once per frame and pass the result explicitly.
  std::string sprite_id;
  std::string collider_id;

  // Animation playback state deliberately lives elsewhere. It is runtime
  // simulation state, not authored data, and keeping it here meant every saved
  // level carried a frame index nothing ever read. See editor/animator.h for
  // how playback state is owned today.
};

}  // namespace zebes