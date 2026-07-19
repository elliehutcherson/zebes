#include "editor/level_editor/viewport_scene.h"

#include "absl/status/status.h"
#include "editor/level_editor/parallax_layout.h"

namespace zebes {
namespace {

bool Intersects(const WorldRect& rect, const VisibleWorldBounds& visible) {
  return rect.max.x >= visible.min.x && rect.min.x <= visible.max.x &&
         rect.max.y >= visible.min.y && rect.min.y <= visible.max.y;
}

}  // namespace

absl::StatusOr<std::vector<EntityRenderItem>> ComposeEntityRenderItems(
    const std::map<uint64_t, Entity>& entities, const EntityRenderOptions& options) {
  if (options.overlay_opacity < 0.0f || options.overlay_opacity > 1.0f) {
    return absl::InvalidArgumentError("entity overlay opacity must be between zero and one");
  }

  std::vector<EntityRenderItem> items;
  items.reserve(entities.size());
  for (const auto& [id, entity] : entities) {
    if (!entity.active) continue;

    EntityRenderItem item{
        .entity_id = id,
        .overlay_opacity = options.overlay_opacity,
        .show_border = options.show_borders,
        .selected = id == options.selected_entity_id,
    };

    absl::StatusOr<WorldRect> bounds = CalculateEntityBounds(entity);
    if (!bounds.ok()) return bounds.status();
    item.bounds = *bounds;

    if (entity.sprite == nullptr || entity.sprite->frames.empty() ||
        !entity.sprite->texture_handle) {
      items.push_back(item);
      continue;
    }

    const SpriteFrame& frame = entity.sprite->frames.front();
    const PixelRect source{
        .x = frame.texture_x,
        .y = frame.texture_y,
        .width = frame.texture_w,
        .height = frame.texture_h,
    };
    if (!source.IsValid()) {
      return absl::InvalidArgumentError("entity sprite frame has invalid texture geometry");
    }

    item.sprite = SpriteRenderItem{
        .texture = entity.sprite->texture_handle,
        .source = source,
    };
    items.push_back(item);
  }
  return items;
}

absl::StatusOr<std::vector<ZoneGizmoItem>> ComposeZoneGizmoItems(
    const std::vector<ParallaxZone>& zones, const Camera& camera,
    std::optional<int> selected_zone_id, std::optional<int> active_zone_id) {
  if (camera.zoom <= 0.0 || camera.viewport_width <= 0 || camera.viewport_height <= 0) {
    return absl::InvalidArgumentError("camera must have positive zoom and viewport dimensions");
  }

  const VisibleWorldBounds visible = CalculateVisibleWorldBounds(camera);
  std::vector<ZoneGizmoItem> items;
  items.reserve(zones.size());
  for (const ParallaxZone& zone : zones) {
    const WorldRect bounds{.min = zone.min_point, .max = zone.max_point};
    if (!bounds.IsValid()) {
      return absl::InvalidArgumentError("parallax zone has invalid bounds");
    }
    if (!Intersects(bounds, visible)) continue;

    ZoneGizmoState state = ZoneGizmoState::kNormal;
    if (active_zone_id == zone.id) state = ZoneGizmoState::kActive;
    if (selected_zone_id == zone.id) state = ZoneGizmoState::kSelected;
    items.push_back(ZoneGizmoItem{
        .zone_id = zone.id,
        .bounds = bounds,
        .state = state,
    });
  }
  return items;
}

}  // namespace zebes
