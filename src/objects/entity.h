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

  uint64_t id = kInvalidId;
  // Inactive entities are kept and saved, but skipped when rendering.
  bool active = true;

  Transform transform;
  // Authored physical properties only. Velocity and acceleration are runtime
  // simulation state and live in Motion.
  Body body;

  // Draw order among entities, low to high: an entity with a smaller sort_order
  // is drawn first and therefore appears behind.
  //
  // This is ordering within one depth slice, not a global depth. Draw passes are
  // fixed -- parallax, then tiles, then entities -- so this cannot put an entity
  // behind the terrain; a layer concept is what would. It is named for the
  // narrower meaning so that it keeps that meaning when layers arrive, rather
  // than becoming a field called "depth" that no longer decides depth.
  //
  // Ties keep insertion order, so entities placed before this existed do not
  // move relative to each other.
  int sort_order = 0;

  std::string blueprint_id;
  int blueprint_state_index = 0;

  // Assets are named, never pointed at; the managers own them. Rendering and
  // picking resolve these once per frame and pass the result explicitly.
  std::string sprite_id;
  std::string collider_id;

  // Animation playback state deliberately lives elsewhere. It is runtime
  // simulation state, not authored data, and keeping it here meant every saved
  // level carried a frame index nothing ever read. See engine/animation.h for
  // the shared playback cursor and game/runtime_world.h for runtime ownership.

  bool operator==(const Entity& other) const = default;
};

}  // namespace zebes
