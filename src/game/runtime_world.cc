#include "game/runtime_world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "engine/input_types.h"
#include "engine/tile_collision.h"
#include "engine/tile_movement.h"
#include "game/player_input.h"
#include "objects/body.h"
#include "objects/collider.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/tileset.h"
#include "objects/transform.h"
#include "objects/vec.h"

namespace zebes {
namespace {

constexpr AxisAlignedBox kPlayerColliderBounds{
    .min = {.x = -16.0, .y = -64.0},
    .max = {.x = 16.0, .y = 0.0},
};

bool IsFinite(Vec value) { return std::isfinite(value.x) && std::isfinite(value.y); }

double MoveToward(double value, double target, double maximum_delta) {
  if (value < target) return std::min(value + maximum_delta, target);
  return std::max(value - maximum_delta, target);
}

AxisAlignedBox TranslateBox(AxisAlignedBox box, Vec position) {
  return {
      .min = {.x = box.min.x + position.x, .y = box.min.y + position.y},
      .max = {.x = box.max.x + position.x, .y = box.max.y + position.y},
  };
}

absl::Status ValidateOccupiedTileIds(const Level& level, const TileCollisionLookup& tiles) {
  for (const WorldLayer& layer : level.layers) {
    for (const auto& [key, chunk] : layer.tile_chunks) {
      const TileChunkCoordinate chunk_coordinate = DecodeChunkKey(key);
      for (int index = 0; index < static_cast<int>(chunk.tiles.size()); ++index) {
        const int tile_id = chunk.tiles[index];
        if (tile_id == 0) continue;
        if (tiles.contains(tile_id)) continue;
        const int tile_x = chunk_coordinate.x * TileChunk::kSize + index % TileChunk::kSize;
        const int tile_y = chunk_coordinate.y * TileChunk::kSize + index / TileChunk::kSize;
        return absl::FailedPreconditionError(
            absl::StrCat("Runtime level references unknown tile ID ", tile_id, " at (", tile_x,
                         ", ", tile_y, ")"));
      }
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<AxisAlignedBox> ValidatePlayerCollider(const Collider& collider) {
  if (collider.id.empty()) {
    return absl::InvalidArgumentError("Runtime player collider must have an ID");
  }
  if (collider.polygons.size() != 1 || collider.polygons.front().size() != 4) {
    return absl::FailedPreconditionError(
        "Runtime player collider must contain one four-corner polygon");
  }

  AxisAlignedBox bounds{.min = collider.polygons.front().front(),
                        .max = collider.polygons.front().front()};
  for (const Vec point : collider.polygons.front()) {
    if (!IsFinite(point)) {
      return absl::InvalidArgumentError("Runtime player collider points must be finite");
    }
    bounds.min.x = std::min(bounds.min.x, point.x);
    bounds.min.y = std::min(bounds.min.y, point.y);
    bounds.max.x = std::max(bounds.max.x, point.x);
    bounds.max.y = std::max(bounds.max.y, point.y);
  }
  if (bounds != kPlayerColliderBounds) {
    return absl::FailedPreconditionError(
        "Runtime player collider must be bottom-centered and exactly 32x64 pixels");
  }

  bool has_top_left = false;
  bool has_top_right = false;
  bool has_bottom_left = false;
  bool has_bottom_right = false;
  for (const Vec point : collider.polygons.front()) {
    has_top_left = has_top_left || point == bounds.min;
    has_top_right = has_top_right || point == Vec{.x = bounds.max.x, .y = bounds.min.y};
    has_bottom_left = has_bottom_left || point == Vec{.x = bounds.min.x, .y = bounds.max.y};
    has_bottom_right = has_bottom_right || point == bounds.max;
  }
  if (!has_top_left || !has_top_right || !has_bottom_left || !has_bottom_right) {
    return absl::FailedPreconditionError(
        "Runtime player collider polygon must contain each 32x64 rectangle corner once");
  }
  return bounds;
}

absl::Status ValidatePlayerEntity(const Entity& player, const Collider& collider) {
  if (!player.active) {
    return absl::FailedPreconditionError("Runtime player entity is inactive");
  }
  if (player.body.is_static) {
    return absl::FailedPreconditionError("Runtime player entity must be dynamic");
  }
  if (player.collider_id != collider.id) {
    return absl::FailedPreconditionError(
        absl::StrCat("Runtime player entity references collider '", player.collider_id,
                     "' but the resolved collider is '", collider.id, "'"));
  }
  if (!IsFinite(player.transform.position) || !std::isfinite(player.transform.rotation)) {
    return absl::InvalidArgumentError("Runtime player transform must be finite");
  }
  if (player.transform.rotation != 0.0F) {
    return absl::FailedPreconditionError("Runtime player collider does not support rotation");
  }
  if (!IsFinite(player.body.drag) || player.body.drag.x < 0.0 || player.body.drag.y < 0.0) {
    return absl::InvalidArgumentError("Runtime player drag must be finite and non-negative");
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status PlayerMovementConfig::Validate() const {
  const std::array values{
      horizontal_acceleration,  horizontal_deceleration,
      maximum_horizontal_speed, gravity,
      maximum_fall_speed,       jump_speed,
  };
  if (!std::ranges::all_of(values,
                           [](double value) { return std::isfinite(value) && value > 0.0; })) {
    return absl::InvalidArgumentError("Player movement values must be finite and positive");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<RuntimeWorld>> RuntimeWorld::Create(Options options) {
  if (options.player_blueprint_id.empty()) {
    return absl::InvalidArgumentError("Runtime player blueprint ID is empty");
  }
  RETURN_IF_ERROR(ValidateLevel(options.level));
  if (options.tileset.id.empty() || options.level.tileset_id != options.tileset.id) {
    return absl::FailedPreconditionError(
        "Runtime level and tileset must have the same non-empty ID");
  }
  ASSIGN_OR_RETURN(const AxisAlignedBox player_collider,
                   ValidatePlayerCollider(options.player_collider));
  ASSIGN_OR_RETURN(TileCollisionLookup collision_tiles, BuildTileCollisionLookup(options.tileset));
  RETURN_IF_ERROR(ValidateOccupiedTileIds(options.level, collision_tiles));

  const Entity* player = nullptr;
  int player_layer_id = -1;
  size_t player_count = 0;
  absl::flat_hash_map<uint64_t, Transform> transforms;
  absl::flat_hash_map<uint64_t, Motion> motions;
  for (const WorldLayer& layer : options.level.layers) {
    for (const auto& [entity_id, entity] : layer.entities) {
      if (entity.blueprint_id == options.player_blueprint_id) {
        player = &entity;
        player_layer_id = layer.id;
        ++player_count;
      }
      if (!entity.active) continue;
      transforms.emplace(entity_id, entity.transform);
      if (!entity.body.is_static) motions.emplace(entity_id, Motion{});
    }
  }

  if (player_count == 0) {
    return absl::NotFoundError(absl::StrCat("Level contains no runtime player for blueprint '",
                                            options.player_blueprint_id, "'"));
  }
  if (player_count > 1) {
    return absl::FailedPreconditionError(
        absl::StrCat("Level contains ", player_count, " runtime players for blueprint '",
                     options.player_blueprint_id, "'; exactly one is required"));
  }
  RETURN_IF_ERROR(ValidatePlayerEntity(*player, options.player_collider));

  const WorldLayer* player_layer = FindWorldLayer(options.level, player_layer_id);
  if (player_layer == nullptr) {
    return absl::InternalError("Runtime player layer disappeared during construction");
  }
  const AxisAlignedBox world_player_collider =
      TranslateBox(player_collider, player->transform.position);
  ASSIGN_OR_RETURN(const TileMovementResult validated_placement,
                   MoveBoxThroughTileLayer({
                       .layer = *player_layer,
                       .tiles = collision_tiles,
                       .tile_width = options.level.tile_render_width,
                       .tile_height = options.level.tile_render_height,
                       .box = world_player_collider,
                   }));
  (void)validated_placement;
  ASSIGN_OR_RETURN(const TileMovementResult ground_probe,
                   MoveBoxThroughTileLayer({
                       .layer = *player_layer,
                       .tiles = collision_tiles,
                       .tile_width = options.level.tile_render_width,
                       .tile_height = options.level.tile_render_height,
                       .box = world_player_collider,
                       .displacement = {.x = 0.0, .y = 1e-6},
                       .velocity = {.x = 0.0, .y = 1e-6},
                   }));

  const uint64_t player_entity_id = player->id;
  const Body player_body = player->body;
  absl::flat_hash_map<uint64_t, PlayerControllerState> player_controllers;
  player_controllers.emplace(player_entity_id,
                             PlayerControllerState{.grounded = ground_probe.grounded});
  return std::unique_ptr<RuntimeWorld>(new RuntimeWorld({
      .level = std::move(options.level),
      .player_entity_id = player_entity_id,
      .player_layer_id = player_layer_id,
      .player_local_collider = player_collider,
      .player_body = player_body,
      .collision_tiles = std::move(collision_tiles),
      .transforms = std::move(transforms),
      .motions = std::move(motions),
      .player_controllers = std::move(player_controllers),
  }));
}

RuntimeWorld::RuntimeWorld(InitialState state)
    : level_(std::move(state.level)),
      player_entity_id_(state.player_entity_id),
      player_layer_id_(state.player_layer_id),
      player_local_collider_(state.player_local_collider),
      player_body_(state.player_body),
      collision_tiles_(std::move(state.collision_tiles)),
      transforms_(std::move(state.transforms)),
      motions_(std::move(state.motions)),
      player_controllers_(std::move(state.player_controllers)) {}

const Transform* RuntimeWorld::FindTransform(uint64_t entity_id) const {
  const auto found = transforms_.find(entity_id);
  return found == transforms_.end() ? nullptr : &found->second;
}

Transform* RuntimeWorld::FindTransform(uint64_t entity_id) {
  auto found = transforms_.find(entity_id);
  return found == transforms_.end() ? nullptr : &found->second;
}

const Motion* RuntimeWorld::FindMotion(uint64_t entity_id) const {
  const auto found = motions_.find(entity_id);
  return found == motions_.end() ? nullptr : &found->second;
}

Motion* RuntimeWorld::FindMotion(uint64_t entity_id) {
  auto found = motions_.find(entity_id);
  return found == motions_.end() ? nullptr : &found->second;
}

const PlayerControllerState* RuntimeWorld::FindPlayerController(uint64_t entity_id) const {
  const auto found = player_controllers_.find(entity_id);
  return found == player_controllers_.end() ? nullptr : &found->second;
}

PlayerControllerState* RuntimeWorld::FindPlayerController(uint64_t entity_id) {
  auto found = player_controllers_.find(entity_id);
  return found == player_controllers_.end() ? nullptr : &found->second;
}

void RuntimeWorld::ApplyPlayerInput(const InputSnapshot& input) {
  auto player = player_controllers_.find(player_entity_id_);
  ABSL_CHECK(player != player_controllers_.end())
      << "Runtime player controller registry lost its player";
  player->second.intent = BuildPlayerInputIntent(input, player->second.previous_input);
  player->second.previous_input = input;
}

absl::Status RuntimeWorld::StepPlayer(const InputSnapshot& input, double delta_seconds,
                                      const PlayerMovementConfig& config) {
  if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0) {
    return absl::InvalidArgumentError("Runtime player step must be finite and positive");
  }
  RETURN_IF_ERROR(config.Validate());

  const auto transform_entry = transforms_.find(player_entity_id_);
  const auto motion_entry = motions_.find(player_entity_id_);
  const auto controller_entry = player_controllers_.find(player_entity_id_);
  const WorldLayer* player_layer = FindWorldLayer(level_, player_layer_id_);
  if (transform_entry == transforms_.end() || motion_entry == motions_.end() ||
      controller_entry == player_controllers_.end() || player_layer == nullptr) {
    return absl::FailedPreconditionError("Runtime player registries are incomplete");
  }

  Transform next_transform = transform_entry->second;
  Motion next_motion = motion_entry->second;
  PlayerControllerState next_controller = controller_entry->second;
  next_controller.intent = BuildPlayerInputIntent(input, next_controller.previous_input);

  const double horizontal_target =
      next_controller.intent.horizontal_axis * config.maximum_horizontal_speed;
  const double horizontal_rate = next_controller.intent.horizontal_axis == 0
                                     ? config.horizontal_deceleration
                                     : config.horizontal_acceleration;
  const double previous_horizontal_velocity = next_motion.velocity.x;
  next_motion.velocity.x =
      MoveToward(next_motion.velocity.x, horizontal_target, horizontal_rate * delta_seconds);
  next_motion.acceleration.x =
      (next_motion.velocity.x - previous_horizontal_velocity) / delta_seconds;

  if (next_controller.grounded && next_controller.intent.jump_pressed) {
    next_motion.velocity.y = -config.jump_speed;
    next_controller.grounded = false;
  }
  if (next_controller.grounded && next_controller.intent.horizontal_axis == 0 &&
      next_motion.velocity.x == 0.0) {
    next_motion = {};
    next_controller.previous_input = input;
    motion_entry->second = next_motion;
    controller_entry->second = next_controller;
    return absl::OkStatus();
  }
  next_motion.acceleration.y = config.gravity;
  next_motion.velocity.y =
      std::min(next_motion.velocity.y + config.gravity * delta_seconds, config.maximum_fall_speed);

  const Vec damping{
      .x = std::max(0.0, 1.0 - player_body_.drag.x * delta_seconds),
      .y = std::max(0.0, 1.0 - player_body_.drag.y * delta_seconds),
  };
  next_motion.velocity.x *= damping.x;
  next_motion.velocity.y *= damping.y;

  const AxisAlignedBox starting_box = TranslateBox(player_local_collider_, next_transform.position);
  ASSIGN_OR_RETURN(const TileMovementResult movement,
                   MoveBoxThroughTileLayer({
                       .layer = *player_layer,
                       .tiles = collision_tiles_,
                       .tile_width = level_.tile_render_width,
                       .tile_height = level_.tile_render_height,
                       .box = starting_box,
                       .displacement = {.x = next_motion.velocity.x * delta_seconds,
                                        .y = next_motion.velocity.y * delta_seconds},
                       .velocity = next_motion.velocity,
                   }));
  next_transform.position.x += movement.box.min.x - starting_box.min.x;
  next_transform.position.y += movement.box.min.y - starting_box.min.y;
  next_motion.velocity = movement.velocity;
  if (movement.grounded && next_controller.intent.horizontal_axis == 0) {
    next_motion.velocity = {};
  }
  next_controller.grounded = movement.grounded;
  next_controller.previous_input = input;

  transform_entry->second = next_transform;
  motion_entry->second = next_motion;
  controller_entry->second = next_controller;
  return absl::OkStatus();
}

}  // namespace zebes
