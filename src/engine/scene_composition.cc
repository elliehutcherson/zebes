#include "engine/scene_composition.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

struct TileChunkRange {
  int min_x = 0;
  int min_y = 0;
  int max_x = -1;
  int max_y = -1;

  bool empty() const { return min_x > max_x || min_y > max_y; }
};

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

absl::StatusOr<int> CheckedChunkCoordinate(double coordinate, const char* boundary) {
  if (!std::isfinite(coordinate) || coordinate < 0.0 ||
      coordinate > static_cast<double>(std::numeric_limits<int>::max())) {
    return absl::OutOfRangeError(
        absl::StrCat("visible tile chunk ", boundary, " exceeds the chunk grid"));
  }

  return static_cast<int>(coordinate);
}

absl::StatusOr<TileChunkRange> VisibleTileChunkRange(const Level& level,
                                                     const VisibleWorldBounds& visible) {
  if (!IsFinite(visible.min) || !IsFinite(visible.max)) {
    return absl::InvalidArgumentError("camera produces non-finite visible world bounds");
  }

  const double min_x = std::max(0.0, visible.min.x);
  const double min_y = std::max(0.0, visible.min.y);
  const double max_x = std::min(level.width, visible.max.x);
  const double max_y = std::min(level.height, visible.max.y);
  if (min_x >= max_x || min_y >= max_y) return TileChunkRange{};

  const double chunk_width = static_cast<double>(TileChunk::kSize) * level.tile_render_width;
  const double chunk_height = static_cast<double>(TileChunk::kSize) * level.tile_render_height;
  ASSIGN_OR_RETURN(const int first_x,
                   CheckedChunkCoordinate(std::floor(min_x / chunk_width), "minimum X"));
  ASSIGN_OR_RETURN(const int first_y,
                   CheckedChunkCoordinate(std::floor(min_y / chunk_height), "minimum Y"));
  ASSIGN_OR_RETURN(const int last_x,
                   CheckedChunkCoordinate(std::ceil(max_x / chunk_width) - 1.0, "maximum X"));
  ASSIGN_OR_RETURN(const int last_y,
                   CheckedChunkCoordinate(std::ceil(max_y / chunk_height) - 1.0, "maximum Y"));

  return TileChunkRange{
      .min_x = first_x,
      .min_y = first_y,
      .max_x = last_x,
      .max_y = last_y,
  };
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
                                                                   const ResolvedSprite& resolved,
                                                                   const Transform& transform,
                                                                   int frame_index) {
  SceneEntityRenderItem item{
      .entity_id = entity_id,
      .sort_order = entity.sort_order,
      .origin = transform.position,
  };

  const Sprite* sprite = resolved.sprite;
  ASSIGN_OR_RETURN(item.bounds, CalculateEntityBounds(transform, sprite, frame_index));
  if (sprite == nullptr || sprite->frames.empty() || !resolved.texture) return item;

  const SpriteFrame& frame = sprite->frames[frame_index];
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
    const std::map<uint64_t, Entity>& entities, const SpriteLookup& sprites,
    const SceneEntityRenderOptions& options) {
  std::vector<SceneEntityRenderItem> items;
  items.reserve(entities.size());
  for (const auto& [id, entity] : entities) {
    if (!entity.active) continue;
    const std::string* sprite_id = &entity.sprite_id;
    if (options.sprite_id_overrides != nullptr) {
      const auto sprite_override = options.sprite_id_overrides->find(id);
      if (sprite_override != options.sprite_id_overrides->end()) {
        sprite_id = &sprite_override->second;
      }
    }
    const ResolvedSprite resolved = FindSprite(sprites, *sprite_id);
    if (options.sprite_id_overrides != nullptr && options.sprite_id_overrides->contains(id) &&
        !sprite_id->empty() && resolved.sprite == nullptr) {
      return absl::FailedPreconditionError(
          absl::StrCat("runtime sprite override references unavailable sprite: ", *sprite_id));
    }

    int frame_index = 0;
    if (options.frame_index_overrides != nullptr) {
      const auto frame_override = options.frame_index_overrides->find(id);
      if (frame_override != options.frame_index_overrides->end()) {
        frame_index = frame_override->second;
        if (resolved.sprite == nullptr || resolved.sprite->frames.empty() || frame_index < 0 ||
            frame_index >= static_cast<int>(resolved.sprite->frames.size())) {
          return absl::InvalidArgumentError(
              absl::StrCat("runtime sprite frame override for entity ", id, " is out of range"));
        }
      }
    }
    const Transform* transform = &entity.transform;
    if (options.transform_overrides != nullptr) {
      const auto override = options.transform_overrides->find(id);
      if (override != options.transform_overrides->end()) transform = &override->second;
    }
    ASSIGN_OR_RETURN(SceneEntityRenderItem item,
                     ComposeSceneEntityRenderItem(id, entity, resolved, *transform, frame_index));
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
  ASSIGN_OR_RETURN(const TileChunkRange visible_chunks, VisibleTileChunkRange(level, visible));

  SceneTileRenderBatch batch{.atlas_texture = options.atlas_texture};
  if (visible_chunks.empty()) return batch;

  // The camera determines which sparse keys to query. Rendering cost therefore
  // depends on visible chunk coordinates, not on the level's stored chunk count.
  for (int64_t chunk_y = visible_chunks.min_y; chunk_y <= visible_chunks.max_y; ++chunk_y) {
    for (int64_t chunk_x = visible_chunks.min_x; chunk_x <= visible_chunks.max_x; ++chunk_x) {
      const TileChunkCoordinate coordinate{
          .x = static_cast<int>(chunk_x),
          .y = static_cast<int>(chunk_y),
      };
      const auto stored_chunk =
          options.layer.tile_chunks.find(ChunkKey(coordinate.x, coordinate.y));
      if (stored_chunk == options.layer.tile_chunks.end()) continue;

      const TileChunk& chunk = stored_chunk->second;

      for (int index = 0; index < TileChunk::kSize * TileChunk::kSize; ++index) {
        const int tile_id = chunk.tiles[index];
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
    const ParallaxTheme& theme, const Camera& camera, const TextureHandleLookup& textures,
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
