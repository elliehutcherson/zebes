#include "editor/level_editor/viewport_scene.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

bool Intersects(const WorldRect& rect, const VisibleWorldBounds& visible) {
  return rect.max.x >= visible.min.x && rect.min.x <= visible.max.x &&
         rect.max.y >= visible.min.y && rect.min.y <= visible.max.y;
}

absl::Status ValidateOpacity(float opacity) {
  if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f) {
    return absl::InvalidArgumentError("overlay opacity must be between zero and one");
  }
  return absl::OkStatus();
}

EntityRenderItem DecorateEntity(SceneEntityRenderItem scene, EntityRenderMode mode,
                                const EntityRenderOptions& options) {
  const bool selected = scene.entity_id == options.selected_entity_id;
  return {
      .mode = mode,
      .entity_id = scene.entity_id,
      .sort_order = scene.sort_order,
      .bounds = scene.bounds,
      .sprite = std::move(scene.sprite),
      .overlay_opacity = options.overlay_opacity,
      .show_border = options.show_borders,
      .selected = selected,
      .origin = scene.origin,
      .show_origin = selected,
      .placement_mode = selected ? options.selected_placement_mode : std::nullopt,
  };
}

}  // namespace

absl::StatusOr<std::vector<EntityRenderItem>> ComposeEntityRenderItems(
    const std::map<uint64_t, Entity>& entities, const SpriteLookup& sprites,
    const EntityRenderOptions& options) {
  RETURN_IF_ERROR(ValidateOpacity(options.overlay_opacity));
  ASSIGN_OR_RETURN(std::vector<SceneEntityRenderItem> scene_items,
                   ComposeSceneEntityRenderItems(entities, sprites));
  std::vector<EntityRenderItem> items;
  items.reserve(scene_items.size());
  for (SceneEntityRenderItem& scene : scene_items) {
    items.push_back(DecorateEntity(std::move(scene), EntityRenderMode::kLevel, options));
  }
  return items;
}

absl::StatusOr<EntityRenderItem> ComposeEntityPlacementItem(Vec world_position,
                                                            const ResolvedSprite& resolved,
                                                            BlueprintPlacementMode placement_mode) {
  if (resolved.sprite != nullptr && (resolved.sprite->frames.empty() || !resolved.texture)) {
    return absl::FailedPreconditionError(
        "entity placement sprite requires a frame and texture resource");
  }
  if (!IsValidBlueprintPlacementMode(placement_mode)) {
    return absl::InvalidArgumentError("entity placement mode is invalid");
  }

  const Entity entity{
      .id = Entity::kInvalidId,
      .transform = {.position = world_position},
  };
  ASSIGN_OR_RETURN(
      SceneEntityRenderItem scene,
      ComposeSceneEntityRenderItem(Entity::kInvalidId, entity, resolved, entity.transform));
  EntityRenderItem item = DecorateEntity(std::move(scene), EntityRenderMode::kPlacementGhost, {});
  item.show_origin = true;
  item.placement_mode = placement_mode;
  return item;
}

absl::StatusOr<std::vector<ZoneGizmoItem>> ComposeZoneGizmoItems(
    const std::vector<ParallaxZone>& zones, const Camera& camera,
    std::optional<int> selected_zone_id, std::optional<int> active_zone_id) {
  RETURN_IF_ERROR(ValidateSceneCamera(camera));

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

absl::StatusOr<TileRenderBatch> ComposeLevelTileRenderBatch(
    const Level& level, const WorldLayer& layer, const Tileset& tileset,
    TextureHandle atlas_texture, const Camera& camera, const TileRenderOptions& options) {
  RETURN_IF_ERROR(ValidateOpacity(options.overlay_opacity));
  ASSIGN_OR_RETURN(SceneTileRenderBatch scene, ComposeSceneLevelTileRenderBatch({
                                                   .level = level,
                                                   .layer = layer,
                                                   .tileset = tileset,
                                                   .atlas_texture = atlas_texture,
                                                   .camera = camera,
                                               }));
  return TileRenderBatch{
      .atlas_texture = scene.atlas_texture,
      .mode = TileRenderMode::kLevel,
      .overlay_opacity = options.overlay_opacity,
      .show_frame = options.show_frame,
      .show_collision = options.show_collision,
      .items = std::move(scene.items),
  };
}

absl::StatusOr<TileRenderBatch> ComposeTilePlacementBatch(const Tile& tile, const Tileset& tileset,
                                                          TextureHandle atlas_texture,
                                                          Vec mouse_world, int tile_render_width,
                                                          int tile_render_height) {
  ASSIGN_OR_RETURN(const TileCoordinate coordinate,
                   WorldToTileCoordinate(mouse_world, tile_render_width, tile_render_height));
  ASSIGN_OR_RETURN(SceneTileRenderItem item, ComposeSceneTileRenderItem({
                                                 .tile = tile,
                                                 .tileset = tileset,
                                                 .tile_x = coordinate.x,
                                                 .tile_y = coordinate.y,
                                                 .tile_render_width = tile_render_width,
                                                 .tile_render_height = tile_render_height,
                                             }));
  TileRenderBatch batch{
      .atlas_texture = atlas_texture,
      .mode = TileRenderMode::kPlacementGhost,
  };
  batch.items.push_back(std::move(item));
  return batch;
}

absl::StatusOr<ParallaxRenderBatch> ComposeParallaxRenderBatch(
    const ParallaxTheme& theme, const Camera& camera,
    const std::map<std::string, TextureHandle>& textures, const ParallaxRenderOptions& options) {
  return ComposeSceneParallaxRenderBatch(theme, camera, textures, options);
}

}  // namespace zebes
