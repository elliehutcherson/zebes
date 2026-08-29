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

absl::StatusOr<WorldRect> CalculateEntityBounds(const Entity& entity, const Sprite* sprite) {
  if (!std::isfinite(entity.transform.position.x) || !std::isfinite(entity.transform.position.y)) {
    return absl::InvalidArgumentError("entity position must be finite");
  }

  constexpr double kDefaultHalfSize = 16.0;
  if (sprite == nullptr || sprite->frames.empty()) {
    return WorldRect{
        .min = {entity.transform.position.x - kDefaultHalfSize,
                entity.transform.position.y - kDefaultHalfSize},
        .max = {entity.transform.position.x + kDefaultHalfSize,
                entity.transform.position.y + kDefaultHalfSize},
    };
  }

  const SpriteFrame& frame = sprite->frames.front();
  const SpriteFrameRenderBounds frame_bounds = CalculateSpriteFrameRenderBounds(frame);
  WorldRect bounds{
      .min = {entity.transform.position.x + frame_bounds.left,
              entity.transform.position.y + frame_bounds.top},
      .max = {entity.transform.position.x + frame_bounds.right,
              entity.transform.position.y + frame_bounds.bottom},
  };
  if (!frame_bounds.IsValid() || !bounds.IsValid()) {
    return absl::InvalidArgumentError("entity sprite frame has invalid render dimensions");
  }
  return bounds;
}

ResolvedSprite FindSprite(const SpriteLookup& sprites, const std::string& sprite_id) {
  auto found = sprites.find(sprite_id);
  if (found == sprites.end()) return ResolvedSprite{};
  return found->second;
}

}  // namespace zebes
