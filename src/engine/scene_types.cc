#include "engine/scene_types.h"

#include <cmath>

#include "absl/status/status.h"

namespace zebes {

absl::Status ValidateSceneCamera(const Camera& camera) {
  if (camera.zoom <= 0.0 || camera.viewport_width <= 0 || camera.viewport_height <= 0) {
    return absl::InvalidArgumentError("camera must have positive zoom and viewport dimensions");
  }
  if (!std::isfinite(camera.position.x) || !std::isfinite(camera.position.y) ||
      !std::isfinite(camera.zoom)) {
    return absl::InvalidArgumentError("camera position and zoom must be finite");
  }
  return absl::OkStatus();
}

VisibleWorldBounds CalculateVisibleWorldBounds(const Camera& camera) {
  const double half_width = camera.viewport_width / (2.0 * camera.zoom);
  const double half_height = camera.viewport_height / (2.0 * camera.zoom);
  return {
      .min = {camera.position.x - half_width, camera.position.y - half_height},
      .max = {camera.position.x + half_width, camera.position.y + half_height},
  };
}

absl::StatusOr<WorldRect> CalculateEntityBounds(const Transform& transform, const Sprite* sprite) {
  return CalculateEntityBounds(transform, sprite, 0);
}

absl::StatusOr<WorldRect> CalculateEntityBounds(const Transform& transform, const Sprite* sprite,
                                                int frame_index) {
  if (!std::isfinite(transform.position.x) || !std::isfinite(transform.position.y)) {
    return absl::InvalidArgumentError("entity position must be finite");
  }

  constexpr double kDefaultHalfSize = 16.0;
  if (sprite == nullptr || sprite->frames.empty()) {
    if (frame_index != 0) {
      return absl::InvalidArgumentError("entity sprite frame index is out of range");
    }
    return WorldRect{
        .min = {transform.position.x - kDefaultHalfSize, transform.position.y - kDefaultHalfSize},
        .max = {transform.position.x + kDefaultHalfSize, transform.position.y + kDefaultHalfSize},
    };
  }

  if (frame_index < 0 || frame_index >= static_cast<int>(sprite->frames.size())) {
    return absl::InvalidArgumentError("entity sprite frame index is out of range");
  }
  const SpriteFrame& frame = sprite->frames[frame_index];
  const SpriteFrameRenderBounds frame_bounds = CalculateSpriteFrameRenderBounds(frame);
  WorldRect bounds{
      .min = {transform.position.x + frame_bounds.left, transform.position.y + frame_bounds.top},
      .max = {transform.position.x + frame_bounds.right,
              transform.position.y + frame_bounds.bottom},
  };
  if (!frame_bounds.IsValid() || !bounds.IsValid()) {
    return absl::InvalidArgumentError("entity sprite frame has invalid render dimensions");
  }
  return bounds;
}

absl::StatusOr<WorldRect> CalculateEntityBounds(const Entity& entity, const Sprite* sprite) {
  return CalculateEntityBounds(entity.transform, sprite);
}

ResolvedSprite FindSprite(const SpriteLookup& sprites, const std::string& sprite_id) {
  auto found = sprites.find(sprite_id);
  if (found == sprites.end()) return ResolvedSprite{};
  return found->second;
}

}  // namespace zebes
