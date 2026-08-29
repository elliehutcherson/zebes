#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "engine/input_types.h"
#include "engine/tile_collision.h"
#include "game/player_input.h"
#include "objects/body.h"
#include "objects/collider.h"
#include "objects/level.h"
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

// Owns immutable authored level data beside mutable, entity-ID-keyed runtime
// registries. Runtime movement changes transforms_ and motions_, never the
// Entity definitions in level_. This class performs no I/O.
class RuntimeWorld {
 public:
  struct Options {
    Level level;
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
  AxisAlignedBox player_local_collider() const { return player_local_collider_; }

  const Transform* FindTransform(uint64_t entity_id) const;
  Transform* FindTransform(uint64_t entity_id);
  const Motion* FindMotion(uint64_t entity_id) const;
  Motion* FindMotion(uint64_t entity_id);
  const PlayerControllerState* FindPlayerController(uint64_t entity_id) const;
  PlayerControllerState* FindPlayerController(uint64_t entity_id);

  // Applies one snapshot for one fixed simulation tick. Reapplying the same
  // snapshot preserves held input but does not repeat jump_pressed.
  void ApplyPlayerInput(const InputSnapshot& input);

 private:
  RuntimeWorld(Level level, uint64_t player_entity_id, AxisAlignedBox player_local_collider,
               absl::flat_hash_map<uint64_t, Transform> transforms,
               absl::flat_hash_map<uint64_t, Motion> motions,
               absl::flat_hash_map<uint64_t, PlayerControllerState> player_controllers);

  Level level_;
  uint64_t player_entity_id_ = 0;
  AxisAlignedBox player_local_collider_;
  absl::flat_hash_map<uint64_t, Transform> transforms_;
  absl::flat_hash_map<uint64_t, Motion> motions_;
  absl::flat_hash_map<uint64_t, PlayerControllerState> player_controllers_;
};

}  // namespace zebes
