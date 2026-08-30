#include "editor/level_editor/viewport_interaction.h"

#include <cmath>
#include <limits>
#include <utility>

#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/level_editor/viewport_model.h"

namespace zebes {
namespace {

absl::Status ValidateInput(const Level& level, const ViewportInteractionInput& input) {
  if (!std::isfinite(level.width) || !std::isfinite(level.height) || level.width < 0.0 ||
      level.height < 0.0) {
    return absl::InvalidArgumentError("level world dimensions must be finite and non-negative");
  }

  const bool has_pointer_action = input.primary_pressed || input.primary_down ||
                                  input.secondary_pressed || input.secondary_down;
  if (has_pointer_action &&
      (!std::isfinite(input.world_position.x) || !std::isfinite(input.world_position.y))) {
    return absl::InvalidArgumentError("viewport interaction position must be finite");
  }

  if (!input.pointer_in_level) return absl::OkStatus();
  if (input.world_position.x < 0.0 || input.world_position.y < 0.0 ||
      input.world_position.x >= level.width || input.world_position.y >= level.height) {
    return absl::InvalidArgumentError("pointer marked inside level is outside its bounds");
  }
  return absl::OkStatus();
}

}  // namespace

void ViewportInteractionController::Reset() {
  next_entity_id_ = 1;
  dragged_entity_id_ = Entity::kInvalidId;
  entity_drag_.Reset();
  last_painted_.reset();
}

absl::StatusOr<ViewportInteractionResult> ViewportInteractionController::Update(
    Level& level, WorldLayer& layer, const ViewportInteractionInput& input,
    const ViewportInteractionOptions& options) {
  RETURN_IF_ERROR(ValidateInput(level, input));

  const int active_modes = static_cast<int>(options.paint_terrain_id.has_value()) +
                           static_cast<int>(options.paint_tile_id.has_value()) +
                           static_cast<int>(options.placement_blueprint != nullptr);
  if (active_modes > 1) {
    return absl::InvalidArgumentError(
        "terrain, tile, and blueprint placement modes are mutually exclusive");
  }

  // A released button ends the current stroke, so the next press may repaint
  // the same cell.
  if (!input.primary_down && !input.secondary_down) last_painted_.reset();

  if (options.paint_terrain_id.has_value()) {
    dragged_entity_id_ = Entity::kInvalidId;
    entity_drag_.Reset();
    return UpdateTerrain(level, layer, input, options);
  }
  if (options.paint_tile_id.has_value()) {
    dragged_entity_id_ = Entity::kInvalidId;
    entity_drag_.Reset();
    return UpdateTile(level, layer, input, *options.paint_tile_id);
  }
  return UpdateEntity(level, layer, input, options);
}

bool ViewportInteractionController::ClaimPaintCell(TileCoordinate coordinate, bool erasing) {
  if (last_painted_.has_value() && last_painted_->coordinate.x == coordinate.x &&
      last_painted_->coordinate.y == coordinate.y && last_painted_->erasing == erasing) {
    return false;
  }
  last_painted_ = PaintedCell{.coordinate = coordinate, .erasing = erasing};
  return true;
}

absl::StatusOr<ViewportInteractionResult> ViewportInteractionController::UpdateTile(
    const Level& level, WorldLayer& layer, const ViewportInteractionInput& input, int tile_id) {
  if (tile_id <= 0) {
    return absl::InvalidArgumentError("paint tile ID must be positive");
  }
  if (level.tile_render_width <= 0 || level.tile_render_height <= 0) {
    return absl::InvalidArgumentError("tile render dimensions must be positive");
  }
  if (!input.pointer_in_level) return ViewportInteractionResult{};

  ASSIGN_OR_RETURN(TileCoordinate coordinate,
                   WorldToTileCoordinate(input.world_position, level.tile_render_width,
                                         level.tile_render_height));
  const bool erasing = !input.primary_down && (input.secondary_pressed || input.secondary_down);
  if (!input.primary_down && !erasing) return ViewportInteractionResult{};
  if (!ClaimPaintCell(coordinate, erasing)) return ViewportInteractionResult{};

  RETURN_IF_ERROR(SetTileAt(layer, coordinate.x, coordinate.y, erasing ? 0 : tile_id));
  return ViewportInteractionResult{};
}

absl::StatusOr<ViewportInteractionResult> ViewportInteractionController::UpdateTerrain(
    const Level& level, WorldLayer& layer, const ViewportInteractionInput& input,
    const ViewportInteractionOptions& options) {
  if (options.terrain_index == nullptr) {
    return absl::InvalidArgumentError("terrain painting requires a terrain index");
  }
  if (options.terrain_provider == nullptr) {
    return absl::InvalidArgumentError("terrain painting requires a tile provider");
  }
  if (level.tile_render_width <= 0 || level.tile_render_height <= 0) {
    return absl::InvalidArgumentError("tile render dimensions must be positive");
  }
  if (!input.pointer_in_level) return ViewportInteractionResult{};

  ASSIGN_OR_RETURN(TileCoordinate coordinate,
                   WorldToTileCoordinate(input.world_position, level.tile_render_width,
                                         level.tile_render_height));
  const bool erasing = !input.primary_down && (input.secondary_pressed || input.secondary_down);
  if (!input.primary_down && !erasing) return ViewportInteractionResult{};
  if (!ClaimPaintCell(coordinate, erasing)) return ViewportInteractionResult{};

  if (!erasing) {
    RETURN_IF_ERROR(PaintTerrain(level, layer, *options.terrain_index, *options.terrain_provider,
                                 *options.paint_terrain_id, options.paint_shape, coordinate.x,
                                 coordinate.y));
    return ViewportInteractionResult{};
  }
  RETURN_IF_ERROR(EraseTerrain(level, layer, *options.terrain_index, *options.terrain_provider,
                               coordinate.x, coordinate.y));
  return ViewportInteractionResult{};
}

absl::StatusOr<ViewportInteractionResult> ViewportInteractionController::UpdateEntity(
    Level& level, WorldLayer& layer, const ViewportInteractionInput& input,
    const ViewportInteractionOptions& options) {
  ViewportInteractionResult result;

  // Picking sizes entities by their sprite. Without a lookup every entity falls
  // back to placeholder bounds, which is correct but coarser.
  static const SpriteLookup* const kNoSprites = new SpriteLookup();
  const SpriteLookup& sprites =
      options.entity_sprites != nullptr ? *options.entity_sprites : *kNoSprites;

  if (options.delete_mode && input.secondary_pressed && input.pointer_in_level) {
    ASSIGN_OR_RETURN(uint64_t picked, PickEntity(layer.entities, input.world_position, sprites));
    if (picked != Entity::kInvalidId) result.delete_entity_id = picked;
    return result;
  }

  if (options.placement_blueprint != nullptr) {
    dragged_entity_id_ = Entity::kInvalidId;
    entity_drag_.Reset();
    if (!input.primary_pressed || !input.pointer_in_level) return result;

    const bool blueprint_references_sprite = options.placement_blueprint->sprite_id(0).has_value();
    if (blueprint_references_sprite && options.placement_sprite == nullptr) {
      return absl::FailedPreconditionError("placement blueprint sprite is unresolved");
    }
    if (!blueprint_references_sprite && options.placement_sprite != nullptr) {
      return absl::InvalidArgumentError("invisible placement blueprint received a sprite");
    }

    ASSIGN_OR_RETURN(const uint64_t available, NextAvailableEntityId(level));
    if (!next_entity_id_.has_value() || available > *next_entity_id_) {
      next_entity_id_ = available;
    }
    if (!next_entity_id_.has_value() || *next_entity_id_ == Entity::kInvalidId) {
      return absl::ResourceExhaustedError("level entity IDs are exhausted");
    }

    if (options.placement_blueprint->states.empty()) {
      return absl::FailedPreconditionError("placement blueprint has no states");
    }
    ASSIGN_OR_RETURN(Entity entity,
                     CreateEntityFromBlueprint(*options.placement_blueprint,
                                               options.placement_blueprint->states.front().key,
                                               input.world_position, *next_entity_id_));
    if (*next_entity_id_ == std::numeric_limits<uint64_t>::max()) {
      next_entity_id_.reset();
    } else {
      ++*next_entity_id_;
    }
    result.placed_entity = std::move(entity);
    return result;
  }

  if (entity_drag_.active()) {
    if (!input.primary_down) {
      dragged_entity_id_ = Entity::kInvalidId;
      entity_drag_.Reset();
      return result;
    }

    auto entity = layer.entities.find(dragged_entity_id_);
    if (entity == layer.entities.end()) {
      dragged_entity_id_ = Entity::kInvalidId;
      entity_drag_.Reset();
      return absl::FailedPreconditionError("dragged entity no longer exists");
    }
    const std::optional<Vec> requested = entity_drag_.Update(input.world_position, true);
    if (!requested.has_value()) {
      return absl::InternalError("active entity drag did not produce a position");
    }
    entity->second.transform.position = *requested;
    return result;
  }

  if (!input.primary_pressed || !input.pointer_in_level) return result;

  ASSIGN_OR_RETURN(uint64_t picked, PickEntity(layer.entities, input.world_position, sprites));
  result.selected_entity_id = picked;
  if (picked == Entity::kInvalidId || picked != options.selected_entity_id) return result;

  auto entity = layer.entities.find(picked);
  if (entity == layer.entities.end()) {
    return absl::InternalError("picked entity is missing from the level");
  }
  dragged_entity_id_ = picked;
  entity_drag_.Begin(input.world_position, entity->second.transform.position);
  return result;
}

}  // namespace zebes
