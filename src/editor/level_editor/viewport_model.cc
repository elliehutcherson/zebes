#include "editor/level_editor/viewport_model.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#include "absl/status/status.h"

namespace zebes {

absl::StatusOr<WorldRect> CalculateEntityBounds(const Entity& entity) {
  constexpr double kDefaultHalfSize = 16.0;
  if (entity.sprite == nullptr || entity.sprite->frames.empty()) {
    return WorldRect{
        .min = {entity.transform.position.x - kDefaultHalfSize,
                entity.transform.position.y - kDefaultHalfSize},
        .max = {entity.transform.position.x + kDefaultHalfSize,
                entity.transform.position.y + kDefaultHalfSize},
    };
  }

  const SpriteFrame& frame = entity.sprite->frames.front();
  WorldRect bounds{
      .min = {entity.transform.position.x + frame.offset_x,
              entity.transform.position.y + frame.offset_y},
      .max = {entity.transform.position.x + frame.offset_x + frame.render_w,
              entity.transform.position.y + frame.offset_y + frame.render_h},
  };
  if (!bounds.IsValid()) {
    return absl::InvalidArgumentError("entity sprite frame has invalid render dimensions");
  }
  return bounds;
}

absl::StatusOr<uint64_t> PickEntity(const std::map<uint64_t, Entity>& entities, Vec world_pos) {
  for (const auto& [id, entity] : entities) {
    if (!entity.active) continue;

    absl::StatusOr<WorldRect> bounds = CalculateEntityBounds(entity);
    if (!bounds.ok()) return bounds.status();
    if (world_pos.x >= bounds->min.x && world_pos.x <= bounds->max.x &&
        world_pos.y >= bounds->min.y && world_pos.y <= bounds->max.y) {
      return id;
    }
  }
  return Entity::kInvalidId;
}

uint64_t NextAvailableEntityId(const std::map<uint64_t, Entity>& entities) {
  if (entities.empty()) return 1;
  return entities.rbegin()->first + 1;
}

Entity CreateEntityFromBlueprint(const Blueprint& blueprint, int state_index, Vec world_pos,
                                 uint64_t id) {
  Entity entity;
  entity.id = id;
  entity.blueprint_id = blueprint.id;
  entity.blueprint_state_index = state_index;
  entity.transform.position = world_pos;
  return entity;
}

int64_t ChunkKey(int chunk_x, int chunk_y) {
  const uint64_t encoded = (static_cast<uint64_t>(static_cast<uint32_t>(chunk_y)) << 32) |
                           static_cast<uint32_t>(chunk_x);
  return std::bit_cast<int64_t>(encoded);
}

TileChunkCoordinate DecodeChunkKey(int64_t key) {
  const uint64_t encoded = std::bit_cast<uint64_t>(key);
  return {
      .x = std::bit_cast<int32_t>(static_cast<uint32_t>(encoded)),
      .y = std::bit_cast<int32_t>(static_cast<uint32_t>(encoded >> 32)),
  };
}

absl::StatusOr<TileCoordinate> WorldToTileCoordinate(
    Vec world_position, int tile_render_width, int tile_render_height) {
  if (tile_render_width <= 0 || tile_render_height <= 0) {
    return absl::InvalidArgumentError("tile render dimensions must be positive");
  }
  if (!std::isfinite(world_position.x) || !std::isfinite(world_position.y)) {
    return absl::InvalidArgumentError("tile position must be finite");
  }

  const long double tile_x =
      std::floor(static_cast<long double>(world_position.x) / tile_render_width);
  const long double tile_y =
      std::floor(static_cast<long double>(world_position.y) / tile_render_height);
  constexpr int kMinCoordinate = std::numeric_limits<int>::min();
  constexpr int kMaxCoordinate = std::numeric_limits<int>::max();
  if (tile_x < kMinCoordinate || tile_x > kMaxCoordinate || tile_y < kMinCoordinate ||
      tile_y > kMaxCoordinate) {
    return absl::OutOfRangeError("tile coordinate exceeds level storage range");
  }
  return TileCoordinate{
      .x = static_cast<int>(tile_x),
      .y = static_cast<int>(tile_y),
  };
}

absl::Status SetTileAt(Level& level, int tile_x, int tile_y, int tile_id) {
  if (tile_x < 0 || tile_y < 0) {
    return absl::InvalidArgumentError("tile coordinates must be non-negative");
  }
  if (tile_id < 0) {
    return absl::InvalidArgumentError("tile ID must be non-negative");
  }

  constexpr int kSize = TileChunk::kSize;
  const int chunk_x = tile_x / kSize;
  const int chunk_y = tile_y / kSize;
  const int local_x = tile_x % kSize;
  const int local_y = tile_y % kSize;
  level.tile_chunks[ChunkKey(chunk_x, chunk_y)].tiles[local_y * kSize + local_x] = tile_id;
  return absl::OkStatus();
}

absl::StatusOr<int> GetTileAt(const Level& level, int tile_x, int tile_y) {
  if (tile_x < 0 || tile_y < 0) {
    return absl::InvalidArgumentError("tile coordinates must be non-negative");
  }

  constexpr int kSize = TileChunk::kSize;
  const int chunk_x = tile_x / kSize;
  const int chunk_y = tile_y / kSize;
  const int local_x = tile_x % kSize;
  const int local_y = tile_y % kSize;
  const auto chunk = level.tile_chunks.find(ChunkKey(chunk_x, chunk_y));
  if (chunk == level.tile_chunks.end()) return 0;
  return chunk->second.tiles[local_y * kSize + local_x];
}

absl::StatusOr<Vec> SnapEntityToGrid(Vec mouse_world, int tile_render_w, int tile_render_h,
                                     const Collider* collider, const Sprite* sprite) {
  absl::StatusOr<TileCoordinate> tile =
      WorldToTileCoordinate(mouse_world, tile_render_w, tile_render_h);
  if (!tile.ok()) return tile.status();

  const double cell_center_x =
      static_cast<double>(tile->x) * tile_render_w + tile_render_w / 2.0;
  const double cell_bottom_y =
      (static_cast<double>(tile->y) + 1.0) * tile_render_h;

  if (collider != nullptr && !collider->polygons.empty()) {
    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double max_y = std::numeric_limits<double>::lowest();
    for (const Polygon& polygon : collider->polygons) {
      for (const Vec& point : polygon) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        max_y = std::max(max_y, point.y);
      }
    }
    return Vec{cell_center_x - (min_x + max_x) / 2.0, cell_bottom_y - max_y};
  }

  if (sprite != nullptr && !sprite->frames.empty()) {
    const SpriteFrame& frame = sprite->frames[0];
    if (frame.render_w <= 0 || frame.render_h <= 0) {
      return absl::InvalidArgumentError("sprite frame has invalid render dimensions");
    }
    return Vec{cell_center_x - (frame.offset_x + frame.render_w / 2.0),
               cell_bottom_y - (frame.offset_y + frame.render_h)};
  }

  return absl::InvalidArgumentError("blueprint has no valid collider or sprite");
}

}  // namespace zebes
