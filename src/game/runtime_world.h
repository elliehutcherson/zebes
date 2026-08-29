#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "engine/input_types.h"
#include "engine/tile_collision.h"
#include "engine/tile_movement.h"
#include "game/player_input.h"
#include "objects/body.h"
#include "objects/collider.h"
#include "objects/level.h"
#include "objects/tileset.h"
#include "objects/transform.h"

namespace zebes {

// Stable authored identity used by M2 until the game has a general player-role
// definition. Names, layer placement, and instance IDs are not identities.
inline constexpr std::string_view kMousePlayerPlaceholderBlueprintId =
    "8be038c0-fd4e-4dc5-9def-2a34946c5c4d";

// Tick-owned state for an entity controlled as the local player. The previous
// raw snapshot is kept here, rather than in a render-frame input manager, so a
// catch-up batch consumes a jump edge on its first fixed tick only.
struct PlayerControllerState {
  PlayerInputIntent intent;
  InputSnapshot previous_input;
  bool grounded = false;
};

// M2 gameplay constants. They stay outside EngineConfig until runtime
// measurements justify host tuning in M5.
struct PlayerMovementConfig {
  double horizontal_acceleration = 1800.0;
  double horizontal_deceleration = 2400.0;
  double maximum_horizontal_speed = 240.0;
  double gravity = 1800.0;
  double maximum_fall_speed = 900.0;
  double jump_speed = 600.0;

  absl::Status Validate() const;
};

// Owns immutable authored level data beside mutable, entity-ID-keyed runtime
// registries. Runtime movement changes transforms_ and motions_, never the
// Entity definitions in level_. This class performs no I/O.
class RuntimeWorld {
 public:
  struct Options {
    Level level;
    Tileset tileset;
    std::string player_blueprint_id;
    Collider player_collider;
  };

  // Requires exactly one entity with player_blueprint_id. The candidate must
  // be active, dynamic, unrotated, and reference player_collider. The collider
  // must be the player's established bottom-centered 32x64 rectangle.
  static absl::StatusOr<std::unique_ptr<RuntimeWorld>> Create(Options options);

  RuntimeWorld(const RuntimeWorld&) = delete;
  RuntimeWorld& operator=(const RuntimeWorld&) = delete;

  const Level& level() const { return level_; }
  uint64_t player_entity_id() const { return player_entity_id_; }
  int player_layer_id() const { return player_layer_id_; }
  AxisAlignedBox player_local_collider() const { return player_local_collider_; }
  const absl::flat_hash_map<uint64_t, Transform>& transforms() const { return transforms_; }

  const Transform* FindTransform(uint64_t entity_id) const;
  Transform* FindTransform(uint64_t entity_id);
  const Motion* FindMotion(uint64_t entity_id) const;
  Motion* FindMotion(uint64_t entity_id);
  const PlayerControllerState* FindPlayerController(uint64_t entity_id) const;
  PlayerControllerState* FindPlayerController(uint64_t entity_id);

  // Applies one snapshot for one fixed simulation tick. Reapplying the same
  // snapshot preserves held input but does not repeat jump_pressed.
  void ApplyPlayerInput(const InputSnapshot& input);

  // Advances the player by one fixed tick and commits controller, motion, and
  // transform state only after collision response succeeds completely.
  absl::Status StepPlayer(const InputSnapshot& input, double delta_seconds,
                          const PlayerMovementConfig& config);

 private:
  struct InitialState {
    Level level;
    uint64_t player_entity_id = 0;
    int player_layer_id = -1;
    AxisAlignedBox player_local_collider;
    Body player_body;
    TileCollisionLookup collision_tiles;
    absl::flat_hash_map<uint64_t, Transform> transforms;
    absl::flat_hash_map<uint64_t, Motion> motions;
    absl::flat_hash_map<uint64_t, PlayerControllerState> player_controllers;
  };

  explicit RuntimeWorld(InitialState state);

  Level level_;
  uint64_t player_entity_id_ = 0;
  int player_layer_id_ = -1;
  AxisAlignedBox player_local_collider_;
  Body player_body_;
  TileCollisionLookup collision_tiles_;
  absl::flat_hash_map<uint64_t, Transform> transforms_;
  absl::flat_hash_map<uint64_t, Motion> motions_;
  absl::flat_hash_map<uint64_t, PlayerControllerState> player_controllers_;
};

}  // namespace zebes
