#include "game/runtime_world.h"

#include <algorithm>
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
#include "game/player_input.h"
#include "objects/body.h"
#include "objects/collider.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/transform.h"
#include "objects/vec.h"

namespace zebes {
namespace {

constexpr AxisAlignedBox kPlayerColliderBounds{
    .min = {.x = -16.0, .y = -64.0},
    .max = {.x = 16.0, .y = 0.0},
};

bool IsFinite(Vec value) { return std::isfinite(value.x) && std::isfinite(value.y); }

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
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<RuntimeWorld>> RuntimeWorld::Create(Options options) {
  if (options.player_blueprint_id.empty()) {
    return absl::InvalidArgumentError("Runtime player blueprint ID is empty");
  }
  RETURN_IF_ERROR(ValidateLevel(options.level));
  ASSIGN_OR_RETURN(const AxisAlignedBox player_collider,
                   ValidatePlayerCollider(options.player_collider));

  const Entity* player = nullptr;
  size_t player_count = 0;
  absl::flat_hash_map<uint64_t, Transform> transforms;
  absl::flat_hash_map<uint64_t, Motion> motions;
  for (const WorldLayer& layer : options.level.layers) {
    for (const auto& [entity_id, entity] : layer.entities) {
      if (entity.blueprint_id == options.player_blueprint_id) {
        player = &entity;
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

  const uint64_t player_entity_id = player->id;
  absl::flat_hash_map<uint64_t, PlayerControllerState> player_controllers;
  player_controllers.emplace(player_entity_id, PlayerControllerState{});
  return std::unique_ptr<RuntimeWorld>(
      new RuntimeWorld(std::move(options.level), player_entity_id, player_collider,
                       std::move(transforms), std::move(motions), std::move(player_controllers)));
}

RuntimeWorld::RuntimeWorld(Level level, uint64_t player_entity_id,
                           AxisAlignedBox player_local_collider,
                           absl::flat_hash_map<uint64_t, Transform> transforms,
                           absl::flat_hash_map<uint64_t, Motion> motions,
                           absl::flat_hash_map<uint64_t, PlayerControllerState> player_controllers)
    : level_(std::move(level)),
      player_entity_id_(player_entity_id),
      player_local_collider_(player_local_collider),
      transforms_(std::move(transforms)),
      motions_(std::move(motions)),
      player_controllers_(std::move(player_controllers)) {}

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

}  // namespace zebes
