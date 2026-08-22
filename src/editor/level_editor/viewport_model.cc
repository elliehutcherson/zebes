#include "editor/level_editor/viewport_model.h"

#include <cmath>
#include <limits>

#include "absl/status/status.h"
#include "common/status_macros.h"

namespace zebes {

absl::StatusOr<WorldRect> CalculateEntityBounds(const Entity& entity, const Sprite* sprite) {
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

absl::StatusOr<uint64_t> PickEntity(const std::map<uint64_t, Entity>& entities, Vec world_pos,
                                    const SpriteLookup& sprites) {
  // Whichever candidate the renderer drew last, because that is the one the user
  // can actually see under the cursor. ComposeEntityRenderItems sorts by
  // sort_order and breaks ties by ascending ID, so the topmost is the greatest
  // (sort_order, id) pair -- and picking has to agree with drawing, or clicking a
  // prop in front selects the one hidden behind it.
  uint64_t picked = Entity::kInvalidId;
  int picked_sort_order = 0;

  for (const auto& [id, entity] : entities) {
    if (!entity.active) continue;

    ASSIGN_OR_RETURN(const WorldRect bounds,
                     CalculateEntityBounds(entity, FindSprite(sprites, entity.sprite_id).sprite));
    if (world_pos.x < bounds.min.x || world_pos.x > bounds.max.x || world_pos.y < bounds.min.y ||
        world_pos.y > bounds.max.y) {
      continue;
    }
    // Ascending ID iteration means >= also settles ties in favour of the later
    // entity, matching the stable sort.
    if (picked == Entity::kInvalidId || entity.sort_order >= picked_sort_order) {
      picked = id;
      picked_sort_order = entity.sort_order;
    }
  }
  return picked;
}

Entity CreateEntityFromBlueprint(const Blueprint& blueprint, int state_index, Vec world_pos,
                                 uint64_t id) {
  Entity entity;
  entity.id = id;
  entity.blueprint_id = blueprint.id;
  entity.blueprint_state_index = state_index;
  entity.transform.position = world_pos;
  // The blueprint state is the authored source of the asset reference, so the
  // entity records the ID rather than a resolved pointer.
  entity.sprite_id = blueprint.sprite_id(state_index).value_or("");
  return entity;
}

absl::StatusOr<TileCoordinate> WorldToTileCoordinate(Vec world_position, int tile_render_width,
                                                     int tile_render_height) {
  if (tile_render_width <= 0 || tile_render_height <= 0) {
    return absl::InvalidArgumentError("tile render dimensions must be positive");
  }
  if (!std::isfinite(world_position.x) || !std::isfinite(world_position.y)) {
    return absl::InvalidArgumentError("tile position must be finite");
  }

  const double tile_x = std::floor(world_position.x / tile_render_width);
  const double tile_y = std::floor(world_position.y / tile_render_height);
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

absl::Status SetTileAt(WorldLayer& layer, int tile_x, int tile_y, int tile_id) {
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
  layer.tile_chunks[ChunkKey(chunk_x, chunk_y)].tiles[local_y * kSize + local_x] = tile_id;
  return absl::OkStatus();
}

absl::StatusOr<int> GetTileAt(const WorldLayer& layer, int tile_x, int tile_y) {
  if (tile_x < 0 || tile_y < 0) {
    return absl::InvalidArgumentError("tile coordinates must be non-negative");
  }

  constexpr int kSize = TileChunk::kSize;
  const int chunk_x = tile_x / kSize;
  const int chunk_y = tile_y / kSize;
  const int local_x = tile_x % kSize;
  const int local_y = tile_y % kSize;
  const auto chunk = layer.tile_chunks.find(ChunkKey(chunk_x, chunk_y));
  if (chunk == layer.tile_chunks.end()) return 0;
  return chunk->second.tiles[local_y * kSize + local_x];
}

PaletteBinding ResolvePaletteBinding(const Level& level, const PaletteSelection& selection) {
  PaletteBinding binding{.tileset_id = level.tileset_id};

  // Exactly one palette mode reports a tileset, so whichever does is the one
  // asking to paint.
  const Tileset* palette_tileset =
      selection.tile_tileset != nullptr ? selection.tile_tileset : selection.terrain_tileset;
  if (palette_tileset == nullptr) return binding;

  // A never-bound level takes the palette's tileset: there are no tile IDs yet
  // for it to reinterpret, and it saves a round trip through the level panel
  // just to start painting.
  if (binding.tileset_id.empty()) binding.tileset_id = palette_tileset->id;

  if (palette_tileset->id != binding.tileset_id) {
    binding.rejected_tileset = palette_tileset;
    return binding;
  }

  binding.tile = selection.tile;
  binding.terrain_id = selection.terrain_id;
  return binding;
}

absl::StatusOr<Vec> SnapBlueprintOriginToGrid(Vec mouse_world, int tile_render_w, int tile_render_h,
                                              BlueprintPlacementMode placement_mode) {
  if (!IsValidBlueprintPlacementMode(placement_mode)) {
    return absl::InvalidArgumentError("blueprint placement mode is invalid");
  }
  ASSIGN_OR_RETURN(const TileCoordinate tile,
                   WorldToTileCoordinate(mouse_world, tile_render_w, tile_render_h));

  const double cell_left = static_cast<double>(tile.x) * tile_render_w;
  const double cell_top = static_cast<double>(tile.y) * tile_render_h;
  const double cell_center_x = cell_left + tile_render_w / 2.0;
  switch (placement_mode) {
    case BlueprintPlacementMode::kGrounded:
      return Vec{cell_center_x, cell_top + tile_render_h};
    case BlueprintPlacementMode::kCeiling:
      return Vec{cell_center_x, cell_top};
    case BlueprintPlacementMode::kFree:
      return Vec{cell_center_x, cell_top + tile_render_h / 2.0};
  }
  return absl::InternalError("validated blueprint placement mode was not handled");
}

absl::StatusOr<Vec> SnapBlueprintOriginToNearestGridAnchor(Vec origin, int tile_render_w,
                                                           int tile_render_h,
                                                           BlueprintPlacementMode placement_mode) {
  if (tile_render_w <= 0 || tile_render_h <= 0) {
    return absl::InvalidArgumentError("tile render dimensions must be positive");
  }
  if (!std::isfinite(origin.x) || !std::isfinite(origin.y)) {
    return absl::InvalidArgumentError("blueprint origin must be finite");
  }
  if (!IsValidBlueprintPlacementMode(placement_mode)) {
    return absl::InvalidArgumentError("blueprint placement mode is invalid");
  }

  const auto nearest_anchor = [](double coordinate, int spacing,
                                 double offset) -> absl::StatusOr<double> {
    const double grid_index = std::round((coordinate - offset) / spacing);
    constexpr double kMinCoordinate = std::numeric_limits<int>::min();
    constexpr double kMaxCoordinate = std::numeric_limits<int>::max();
    if (grid_index < kMinCoordinate || grid_index > kMaxCoordinate) {
      return absl::OutOfRangeError("blueprint origin exceeds level grid range");
    }
    return grid_index * spacing + offset;
  };

  const double half_width = tile_render_w / 2.0;
  ASSIGN_OR_RETURN(const double snapped_x, nearest_anchor(origin.x, tile_render_w, half_width));

  switch (placement_mode) {
    case BlueprintPlacementMode::kGrounded:
    case BlueprintPlacementMode::kCeiling: {
      ASSIGN_OR_RETURN(const double snapped_y, nearest_anchor(origin.y, tile_render_h, 0.0));
      return Vec{snapped_x, snapped_y};
    }
    case BlueprintPlacementMode::kFree: {
      const double half_height = tile_render_h / 2.0;
      ASSIGN_OR_RETURN(const double snapped_y,
                       nearest_anchor(origin.y, tile_render_h, half_height));
      return Vec{snapped_x, snapped_y};
    }
  }
  return absl::InternalError("validated blueprint placement mode was not handled");
}

}  // namespace zebes
