#include "engine/scene_composition.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

bool IntersectsHalfOpen(const WorldRect& rect, const VisibleWorldBounds& visible) {
  return rect.max.x > visible.min.x && rect.min.x < visible.max.x && rect.max.y > visible.min.y &&
         rect.min.y < visible.max.y;
}

absl::Status ValidateTileRenderInputs(const Tileset& tileset, int tile_render_width,
                                      int tile_render_height) {
  if (tileset.tile_width <= 0 || tileset.tile_height <= 0) {
    return absl::InvalidArgumentError("tileset atlas dimensions must be positive");
  }
  if (tile_render_width <= 0 || tile_render_height <= 0) {
    return absl::InvalidArgumentError("tile render dimensions must be positive");
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

SceneTileRenderItem MakeTileRenderItem(const SceneTileRenderOptions& options) {
  const Vec min{
      options.tile_x * static_cast<double>(options.tile_render_width),
      options.tile_y * static_cast<double>(options.tile_render_height),
  };
  return {
      .tile_id = options.tile.id,
      .bounds = {.min = min,
                 .max = {min.x + options.tile_render_width, min.y + options.tile_render_height}},
      .source = {.x = options.tile.source_x,
                 .y = options.tile.source_y,
                 .width = options.tileset.tile_width,
                 .height = options.tileset.tile_height},
      .collision_shape = options.tile.shape,
  };
}

}  // namespace

absl::StatusOr<SceneEntityRenderItem> ComposeSceneEntityRenderItem(uint64_t entity_id,
                                                                   const Entity& entity,
                                                                   const ResolvedSprite& resolved) {
  SceneEntityRenderItem item{
      .entity_id = entity_id,
      .sort_order = entity.sort_order,
      .origin = entity.transform.position,
  };

  const Sprite* sprite = resolved.sprite;
  ASSIGN_OR_RETURN(item.bounds, CalculateEntityBounds(entity, sprite));
  if (sprite == nullptr || sprite->frames.empty() || !resolved.texture) return item;

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
  item.sprite = SceneSpriteRenderResource{
      .texture = resolved.texture,
      .source = source,
  };
  return item;
}

absl::StatusOr<std::vector<SceneEntityRenderItem>> ComposeSceneEntityRenderItems(
    const std::map<uint64_t, Entity>& entities, const SpriteLookup& sprites) {
  std::vector<SceneEntityRenderItem> items;
  items.reserve(entities.size());
  for (const auto& [id, entity] : entities) {
    if (!entity.active) continue;
    ASSIGN_OR_RETURN(
        SceneEntityRenderItem item,
        ComposeSceneEntityRenderItem(id, entity, FindSprite(sprites, entity.sprite_id)));
    items.push_back(std::move(item));
  }

  std::ranges::stable_sort(items, {}, &SceneEntityRenderItem::sort_order);
  return items;
}

absl::StatusOr<SceneTileRenderBatch> ComposeSceneLevelTileRenderBatch(
    const SceneLevelTileRenderOptions& options) {
  const Level& level = options.level;
  const Tileset& tileset = options.tileset;
  RETURN_IF_ERROR(
      ValidateTileRenderInputs(tileset, level.tile_render_width, level.tile_render_height));
  RETURN_IF_ERROR(ValidateSceneCamera(options.camera));
  if (!std::isfinite(level.width) || !std::isfinite(level.height) || level.width < 0.0 ||
      level.height < 0.0) {
    return absl::InvalidArgumentError("level world dimensions must be finite and non-negative");
  }

  ASSIGN_OR_RETURN(const auto tile_lookup, BuildTileLookup(tileset));
  const VisibleWorldBounds visible = CalculateVisibleWorldBounds(options.camera);
  std::map<TileChunkCoordinate, const TileChunk*> visible_chunks;
  for (const auto& [key, chunk] : options.layer.tile_chunks) {
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

  SceneTileRenderBatch batch{.atlas_texture = options.atlas_texture};
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
      const SceneTileRenderItem item = MakeTileRenderItem({
          .tile = *tile->second,
          .tileset = tileset,
          .tile_x = tile_x,
          .tile_y = tile_y,
          .tile_render_width = level.tile_render_width,
          .tile_render_height = level.tile_render_height,
      });
      if (!IntersectsHalfOpen(item.bounds, visible)) continue;
      if (item.bounds.max.x > level.width || item.bounds.max.y > level.height) {
        return absl::InvalidArgumentError("level contains a tile outside its world bounds");
      }
      batch.items.push_back(item);
    }
  }
  return batch;
}

absl::StatusOr<SceneTileRenderItem> ComposeSceneTileRenderItem(
    const SceneTileRenderOptions& options) {
  RETURN_IF_ERROR(ValidateTileRenderInputs(options.tileset, options.tile_render_width,
                                           options.tile_render_height));
  ASSIGN_OR_RETURN(const auto tile_lookup, BuildTileLookup(options.tileset));
  const auto selected_tile = tile_lookup.find(options.tile.id);
  if (selected_tile == tile_lookup.end()) {
    return absl::InvalidArgumentError("tile does not belong to the tileset");
  }
  if (selected_tile->second->source_x != options.tile.source_x ||
      selected_tile->second->source_y != options.tile.source_y) {
    return absl::InvalidArgumentError("tile does not match its tileset definition");
  }
  return MakeTileRenderItem({
      .tile = *selected_tile->second,
      .tileset = options.tileset,
      .tile_x = options.tile_x,
      .tile_y = options.tile_y,
      .tile_render_width = options.tile_render_width,
      .tile_render_height = options.tile_render_height,
  });
}

absl::StatusOr<SceneParallaxRenderBatch> ComposeSceneParallaxRenderBatch(
    const ParallaxTheme& theme, const Camera& camera,
    const std::map<std::string, TextureHandle>& textures,
    const SceneParallaxRenderOptions& options) {
  RETURN_IF_ERROR(ValidateSceneCamera(camera));
  if (!std::isfinite(options.opacity) || options.opacity < 0.0 || options.opacity > 1.0) {
    return absl::InvalidArgumentError("parallax opacity must be between zero and one");
  }
  if (options.layer_index.has_value() &&
      (*options.layer_index < 0 || *options.layer_index >= static_cast<int>(theme.layers.size()))) {
    return absl::InvalidArgumentError("parallax layer selection is out of range");
  }
  if (options.element_id.has_value() && !options.layer_index.has_value()) {
    return absl::InvalidArgumentError("parallax element selection requires an isolated layer");
  }

  SceneParallaxRenderBatch batch{.camera = camera, .opacity = options.opacity};
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

    SceneParallaxRenderItem item{.layer = layer};
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
      return absl::InvalidArgumentError("parallax element selection is unavailable");
    }
    batch.layers.push_back(std::move(item));
  }
  return batch;
}

}  // namespace zebes
