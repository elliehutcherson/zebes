#pragma once

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "engine/texture_handle.h"
#include "objects/camera.h"
#include "objects/entity.h"
#include "objects/sprite.h"
#include "objects/transform.h"
#include "objects/vec.h"

namespace zebes {

// Opposing corners in level/world coordinates, measured in logical pixels.
struct WorldRect {
  Vec min;
  Vec max;

  constexpr bool IsValid() const { return max.x > min.x && max.y > min.y; }
};

// The portion of the world currently visible through a camera.
struct VisibleWorldBounds {
  Vec min;
  Vec max;
};

// A sprite definition paired with the managed handle for its texture.
//
// Sprite is a pure definition and names its texture by ID only, so the handle
// is resolved alongside it rather than stored on it. An invalid handle is an
// ordinary state: the sprite exists but its texture has not loaded.
struct ResolvedSprite {
  const Sprite* sprite = nullptr;
  TextureHandle texture;
};

// Sprites resolved for one scene snapshot, keyed by Sprite::id.
using SpriteLookup = absl::flat_hash_map<std::string, ResolvedSprite>;

// Rejects camera state that cannot produce finite scene geometry.
absl::Status ValidateSceneCamera(const Camera& camera);

VisibleWorldBounds CalculateVisibleWorldBounds(const Camera& camera);

// Returns the world-space bounds used consistently for rendering and picking.
// A null sprite, or one with no frames, uses a centered 32x32 placeholder.
absl::StatusOr<WorldRect> CalculateEntityBounds(const Entity& entity, const Sprite* sprite);

// Runtime composition supplies a transient transform without copying or
// mutating the authored Entity. Editor and authoring callers use the overload
// above when the persisted transform is authoritative.
absl::StatusOr<WorldRect> CalculateEntityBounds(const Transform& transform, const Sprite* sprite);

// Returns the sprite and texture for an ID. An absent ID yields a default
// ResolvedSprite, whose null sprite callers already handle as unresolved.
ResolvedSprite FindSprite(const SpriteLookup& sprites, const std::string& sprite_id);

}  // namespace zebes
