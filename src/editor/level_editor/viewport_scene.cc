#include "editor/level_editor/viewport_scene.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/level_editor/parallax_layout.h"

namespace zebes {
namespace {

bool Intersects(const WorldRect& rect, const VisibleWorldBounds& visible) {
  return rect.max.x >= visible.min.x && rect.min.x <= visible.max.x &&
         rect.max.y >= visible.min.y && rect.min.y <= visible.max.y;
}

bool IntersectsHalfOpen(const WorldRect& rect, const VisibleWorldBounds& visible) {
  return rect.max.x > visible.min.x && rect.min.x < visible.max.x && rect.max.y > visible.min.y &&
         rect.min.y < visible.max.y;
}

absl::Status ValidateCamera(const Camera& camera) {
  if (camera.zoom <= 0.0 || camera.viewport_width <= 0 || camera.viewport_height <= 0) {
    return absl::InvalidArgumentError("camera must have positive zoom and viewport dimensions");
  }
  if (!std::isfinite(camera.position.x) || !std::isfinite(camera.position.y) ||
      !std::isfinite(camera.zoom)) {
    return absl::InvalidArgumentError("camera position and zoom must be finite");
  }
  return absl::OkStatus();
}

absl::Status ValidateTileRenderInputs(const Tileset& tileset, int tile_render_width,
                                      int tile_render_height, float overlay_opacity) {
  if (tileset.tile_width <= 0 || tileset.tile_height <= 0) {
    return absl::InvalidArgumentError("tileset atlas dimensions must be positive");
  }
  if (tile_render_width <= 0 || tile_render_height <= 0) {
    return absl::InvalidArgumentError("tile render dimensions must be positive");
  }
  if (!std::isfinite(overlay_opacity) || overlay_opacity < 0.0f || overlay_opacity > 1.0f) {
    return absl::InvalidArgumentError("tile overlay opacity must be between zero and one");
  }
  return absl::OkStatus();
}

absl::StatusOr<absl::flat_hash_map<int, const Tile*>> BuildTileLookup(const Tileset& tileset) {
  absl::flat_hash_map<int, const Tile*> tiles;
  tiles.reserve(tileset.tiles.size());
  for (const Tile& tile : tileset.tiles) {
    if (tile.id <= 0) {
      return absl::InvalidArgumentError("tileset tile IDs must be positive");
    }
    if (tile.source_x < 0 || tile.source_y < 0) {
      return absl::InvalidArgumentError("tileset tile source coordinates must be non-negative");
    }
    if (!tiles.emplace(tile.id, &tile).second) {
      return absl::InvalidArgumentError(absl::StrCat("duplicate tileset tile ID: ", tile.id));
    }
  }
  return tiles;
}

TileRenderItem MakeTileRenderItem(const Tile& tile, const Tileset& tileset, int64_t tile_x,
                                  int64_t tile_y, int tile_render_width, int tile_render_height) {
  const Vec min{
      tile_x * static_cast<double>(tile_render_width),
      tile_y * static_cast<double>(tile_render_height),
  };
  return {
      .tile_id = tile.id,
      .bounds = {.min = min, .max = {min.x + tile_render_width, min.y + tile_render_height}},
      .source = {.x = tile.source_x,
                 .y = tile.source_y,
                 .width = tileset.tile_width,
                 .height = tileset.tile_height},
      .collision_shape = tile.shape,
  };
}

absl::StatusOr<EntityRenderItem> ComposeEntityRenderItem(uint64_t entity_id, const Entity& entity,
                                                         const ResolvedSprite& resolved,
                                                         EntityRenderMode mode,
                                                         const EntityRenderOptions& options) {
  EntityRenderItem item{
      .mode = mode,
      .entity_id = entity_id,
      .sort_order = entity.sort_order,
      .overlay_opacity = options.overlay_opacity,
      .show_border = options.show_borders,
      .selected = entity_id == options.selected_entity_id,
      .origin = entity.transform.position,
  };
  item.show_origin = item.selected;
  if (item.selected) item.placement_mode = options.selected_placement_mode;

  const Sprite* sprite = resolved.sprite;
  ASSIGN_OR_RETURN(item.bounds, CalculateEntityBounds(entity, sprite));

  if (sprite == nullptr || sprite->frames.empty() || !resolved.texture) {
    return item;
  }

  const SpriteFrame& frame = sprite->frames.front();
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
      .texture = resolved.texture,
      .source = source,
  };
  return item;
}

}  // namespace

absl::StatusOr<std::vector<EntityRenderItem>> ComposeEntityRenderItems(
    const std::map<uint64_t, Entity>& entities, const SpriteLookup& sprites,
    const EntityRenderOptions& options) {
  if (!std::isfinite(options.overlay_opacity) || options.overlay_opacity < 0.0f ||
      options.overlay_opacity > 1.0f) {
    return absl::InvalidArgumentError("entity overlay opacity must be between zero and one");
  }

  std::vector<EntityRenderItem> items;
  items.reserve(entities.size());
  for (const auto& [id, entity] : entities) {
    if (!entity.active) continue;
    ASSIGN_OR_RETURN(EntityRenderItem item,
                     ComposeEntityRenderItem(id, entity, FindSprite(sprites, entity.sprite_id),
                                             EntityRenderMode::kLevel, options));
    items.push_back(std::move(item));
  }

  // Back to front, so the caller draws in order and the last item drawn is the
  // topmost. The sort is stable and the source is a map keyed by ID, so entities
  // sharing a sort_order keep ascending-ID order -- which is exactly what every
  // level authored before this field had, and why the migration writes zero
  // rather than a spread of values.
  std::stable_sort(items.begin(), items.end(),
                   [](const EntityRenderItem& a, const EntityRenderItem& b) {
                     return a.sort_order < b.sort_order;
                   });
  return items;
}

absl::StatusOr<EntityRenderItem> ComposeEntityPlacementItem(Vec world_position,
                                                            const ResolvedSprite& resolved,
                                                            BlueprintPlacementMode placement_mode) {
  if (!std::isfinite(world_position.x) || !std::isfinite(world_position.y)) {
    return absl::InvalidArgumentError("entity placement position must be finite");
  }
  if (resolved.sprite != nullptr && (resolved.sprite->frames.empty() || !resolved.texture)) {
    return absl::FailedPreconditionError(
        "entity placement sprite requires a frame and texture resource");
  }
  if (!IsValidBlueprintPlacementMode(placement_mode)) {
    return absl::InvalidArgumentError("entity placement mode is invalid");
  }

  Entity entity{
      .id = Entity::kInvalidId,
      .transform = {.position = world_position},
  };
  ASSIGN_OR_RETURN(EntityRenderItem item,
                   ComposeEntityRenderItem(Entity::kInvalidId, entity, resolved,
                                           EntityRenderMode::kPlacementGhost, {}));
  item.show_origin = true;
  item.placement_mode = placement_mode;
  return item;
}

absl::StatusOr<std::vector<ZoneGizmoItem>> ComposeZoneGizmoItems(
    const std::vector<ParallaxZone>& zones, const Camera& camera,
    std::optional<int> selected_zone_id, std::optional<int> active_zone_id) {
  RETURN_IF_ERROR(ValidateCamera(camera));

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
  RETURN_IF_ERROR(ValidateTileRenderInputs(tileset, level.tile_render_width,
                                           level.tile_render_height, options.overlay_opacity));
  RETURN_IF_ERROR(ValidateCamera(camera));
  if (!std::isfinite(level.width) || !std::isfinite(level.height) || level.width < 0.0 ||
      level.height < 0.0) {
    return absl::InvalidArgumentError("level world dimensions must be finite and non-negative");
  }

  ASSIGN_OR_RETURN(const auto tile_lookup, BuildTileLookup(tileset));

  const VisibleWorldBounds visible = CalculateVisibleWorldBounds(camera);
  std::map<TileChunkCoordinate, const TileChunk*> visible_chunks;
  for (const auto& [key, chunk] : layer.tile_chunks) {
    const TileChunkCoordinate coordinate = DecodeChunkKey(key);
    if (coordinate.x < 0 || coordinate.y < 0) {
      return absl::InvalidArgumentError("level contains a tile chunk with negative coordinates");
    }

    const Vec chunk_min{
        static_cast<double>(coordinate.x) * TileChunk::kSize * level.tile_render_width,
        static_cast<double>(coordinate.y) * TileChunk::kSize * level.tile_render_height,
    };
    const WorldRect chunk_bounds{
        .min = chunk_min,
        .max = {chunk_min.x + static_cast<double>(TileChunk::kSize) * level.tile_render_width,
                chunk_min.y + static_cast<double>(TileChunk::kSize) * level.tile_render_height},
    };
    if (IntersectsHalfOpen(chunk_bounds, visible)) {
      visible_chunks.emplace(coordinate, &chunk);
    }
  }

  TileRenderBatch batch{
      .atlas_texture = atlas_texture,
      .mode = TileRenderMode::kLevel,
      .overlay_opacity = options.overlay_opacity,
      .show_frame = options.show_frame,
      .show_collision = options.show_collision,
  };
  for (const auto& [coordinate, chunk] : visible_chunks) {
    for (int index = 0; index < TileChunk::kSize * TileChunk::kSize; ++index) {
      const int tile_id = chunk->tiles[index];
      if (tile_id == 0) continue;

      const auto tile = tile_lookup.find(tile_id);
      if (tile == tile_lookup.end()) {
        return absl::InvalidArgumentError(
            absl::StrCat("level references unknown tile ID: ", tile_id));
      }

      const int64_t tile_x =
          static_cast<int64_t>(coordinate.x) * TileChunk::kSize + index % TileChunk::kSize;
      const int64_t tile_y =
          static_cast<int64_t>(coordinate.y) * TileChunk::kSize + index / TileChunk::kSize;
      TileRenderItem item = MakeTileRenderItem(*tile->second, tileset, tile_x, tile_y,
                                               level.tile_render_width, level.tile_render_height);
      if (!IntersectsHalfOpen(item.bounds, visible)) continue;
      if (item.bounds.max.x > level.width || item.bounds.max.y > level.height) {
        return absl::InvalidArgumentError("level contains a tile outside its world bounds");
      }
      batch.items.push_back(item);
    }
  }
  return batch;
}

absl::StatusOr<TileRenderBatch> ComposeTilePlacementBatch(const Tile& tile, const Tileset& tileset,
                                                          TextureHandle atlas_texture,
                                                          Vec mouse_world, int tile_render_width,
                                                          int tile_render_height) {
  RETURN_IF_ERROR(ValidateTileRenderInputs(tileset, tile_render_width, tile_render_height, 0.0f));
  ASSIGN_OR_RETURN(const TileCoordinate coordinate,
                   WorldToTileCoordinate(mouse_world, tile_render_width, tile_render_height));

  ASSIGN_OR_RETURN(const auto tile_lookup, BuildTileLookup(tileset));
  const auto selected_tile = tile_lookup.find(tile.id);
  if (selected_tile == tile_lookup.end()) {
    return absl::InvalidArgumentError("placement tile does not belong to the tileset");
  }
  if (selected_tile->second->source_x != tile.source_x ||
      selected_tile->second->source_y != tile.source_y) {
    return absl::InvalidArgumentError("placement tile does not match its tileset definition");
  }

  TileRenderBatch batch{
      .atlas_texture = atlas_texture,
      .mode = TileRenderMode::kPlacementGhost,
  };
  batch.items.push_back(MakeTileRenderItem(*selected_tile->second, tileset, coordinate.x,
                                           coordinate.y, tile_render_width, tile_render_height));
  return batch;
}

absl::StatusOr<ParallaxRenderBatch> ComposeParallaxRenderBatch(
    const ParallaxTheme& theme, const Camera& camera,
    const std::map<std::string, TextureHandle>& textures, const ParallaxRenderOptions& options) {
  RETURN_IF_ERROR(ValidateCamera(camera));
  if (options.layer_index.has_value() &&
      (*options.layer_index < 0 || *options.layer_index >= static_cast<int>(theme.layers.size()))) {
    return absl::InvalidArgumentError("parallax preview layer index is out of range");
  }
  if (options.element_id.has_value() && !options.layer_index.has_value()) {
    return absl::InvalidArgumentError("parallax element preview requires an isolated layer");
  }

  ParallaxRenderBatch batch{.camera = camera};
  batch.layers.reserve(options.layer_index.has_value() ? 1 : theme.layers.size());
  for (int index = 0; index < static_cast<int>(theme.layers.size()); ++index) {
    if (options.layer_index.has_value() && index != *options.layer_index) continue;
    const ParallaxLayer& layer = theme.layers[index];
    if (!std::isfinite(layer.scroll_factor.x) || !std::isfinite(layer.scroll_factor.y) ||
        !std::isfinite(layer.offset.x) || !std::isfinite(layer.offset.y) ||
        !std::isfinite(layer.repeat_period.x) || !std::isfinite(layer.repeat_period.y) ||
        layer.repeat_period.x < 0.0 || layer.repeat_period.y < 0.0 || layer.elements.empty()) {
      return absl::InvalidArgumentError("parallax layer geometry is invalid");
    }

    ParallaxRenderItem item{.layer = layer};
    if (options.element_id.has_value()) item.layer.elements.clear();
    item.elements.reserve(layer.elements.size());
    for (const ParallaxElement& element : layer.elements) {
      if (options.element_id.has_value() && element.id != *options.element_id) continue;
      if (element.id < 0 || element.texture_id.empty() || !std::isfinite(element.position.x) ||
          !std::isfinite(element.position.y) || !std::isfinite(element.scale) ||
          element.scale <= 0.0f) {
        return absl::InvalidArgumentError("parallax element geometry is invalid");
      }
      auto texture = textures.find(element.texture_id);
      if (texture == textures.end() || !texture->second) {
        return absl::FailedPreconditionError(
            absl::StrCat("parallax texture is unavailable: ", element.texture_id));
      }
      if (options.element_id.has_value()) item.layer.elements.push_back(element);
      item.elements.push_back({.element_id = element.id, .texture = texture->second});
    }
    if (item.elements.empty()) {
      return absl::InvalidArgumentError("parallax preview element ID is unavailable");
    }
    batch.layers.push_back(std::move(item));
  }
  return batch;
}

}  // namespace zebes
