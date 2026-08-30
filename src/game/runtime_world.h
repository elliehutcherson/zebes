#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "engine/animation.h"
#include "engine/collision.h"
#include "engine/input_types.h"
#include "engine/tile_collision.h"
#include "engine/tile_movement.h"
#include "game/player_input.h"
#include "objects/blueprint.h"
#include "objects/body.h"
#include "objects/collider.h"
#include "objects/level.h"
#include "objects/sprite.h"
#include "objects/tileset.h"
#include "objects/transform.h"
#include "resources/loaded_level_assets.h"

namespace zebes {

// Stable authored identity used by the current local-player contract. Names,
// layer placement, and instance IDs are not identities.
inline constexpr std::string_view kPlayerBlueprintId = "1be81945-b011-4342-9109-a10c4040078c";

enum class PlayerFacing : int8_t {
  kLeft = -1,
  kRight = 1,
};

// Tick-owned state for an entity controlled as the local player. The previous
// raw snapshot is kept here, rather than in a render-frame input manager, so a
// catch-up batch consumes a jump edge on its first fixed tick only.
struct PlayerControllerState {
  PlayerInputIntent intent;
  InputSnapshot previous_input;
  bool grounded = false;
  PlayerFacing facing = PlayerFacing::kRight;
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

class RuntimeWorld;

// A boot-resolved state within one immutable Blueprint definition. Keeping the
// definition identity in the handle prevents an index resolved for one
// Blueprint from being applied to another. Only RuntimeWorld can construct a
// valid handle.
class ResolvedBlueprintState {
 public:
  int index() const { return state_index_; }

 private:
  friend class RuntimeWorld;

  ResolvedBlueprintState(const Blueprint* definition, int state_index)
      : definition_(definition), state_index_(state_index) {}

  const Blueprint* definition_ = nullptr;
  int state_index_ = -1;
};

// Borrows one immutable loaded-level definition graph beside mutable,
// entity-ID-keyed runtime registries. Runtime movement changes transforms_ and
// motions_, never the authored Entity definitions. The current checkpoint
// materializes authored entities only; runtime spawn/despawn requires a runtime
// entity roster and a transactional lifecycle API rather than direct insertion
// into these maps. This class performs no I/O.
class RuntimeWorld {
 public:
  struct Options {
    std::string player_blueprint_id;
  };

  // Borrows content, which must outlive the returned RuntimeWorld. Requires
  // exactly one entity with player_blueprint_id. The candidate must be active,
  // dynamic, unrotated, and use the established bottom-centered 32x64 collider.
  static absl::StatusOr<std::unique_ptr<RuntimeWorld>> Create(const LoadedLevelContent& content,
                                                              Options options);

  RuntimeWorld(const RuntimeWorld&) = delete;
  RuntimeWorld& operator=(const RuntimeWorld&) = delete;

  const Level& level() const { return content_.level; }
  uint64_t player_entity_id() const { return player_entity_id_; }
  int player_layer_id() const { return player_layer_id_; }
  AxisAlignedBox player_local_collider() const { return player_local_collider_; }
  const absl::flat_hash_map<uint64_t, Transform>& transforms() const { return transforms_; }
  const absl::flat_hash_map<uint64_t, std::string>& sprite_ids() const { return sprite_ids_; }
  const absl::flat_hash_map<uint64_t, int>& frame_indices() const { return frame_indices_; }

  const Transform* FindTransform(uint64_t entity_id) const;
  Transform* FindTransform(uint64_t entity_id);
  const Motion* FindMotion(uint64_t entity_id) const;
  Motion* FindMotion(uint64_t entity_id);
  const PlayerControllerState* FindPlayerController(uint64_t entity_id) const;
  PlayerControllerState* FindPlayerController(uint64_t entity_id);
  const int* FindBlueprintStateIndex(uint64_t entity_id) const;

  // Applies one snapshot for one fixed simulation tick. Reapplying the same
  // snapshot preserves held input but does not repeat jump_pressed.
  void ApplyPlayerInput(const InputSnapshot& input);

  // Advances the player by one fixed tick and commits controller, motion, and
  // transform state only after collision response succeeds completely.
  absl::Status StepPlayer(const InputSnapshot& input, double delta_seconds,
                          const PlayerMovementConfig& config);

  // Resolves a stable authored key once, then applies the resulting handle
  // without string or Blueprint-catalog lookup. Playback resets only when the
  // selected state actually changes. Until entity collision response exists,
  // a player state transition must retain the established M2 collider.
  absl::StatusOr<ResolvedBlueprintState> ResolveEntityBlueprintState(
      uint64_t entity_id, std::string_view state_key) const;
  absl::Status SetEntityBlueprintState(uint64_t entity_id, const ResolvedBlueprintState& state);

  // Advances every active sprite cursor by one fixed simulation tick. Runtime
  // construction and state selection establish all invariants, so this cannot
  // fail or allocate during ordinary frame advancement.
  void AdvanceAnimations();

 private:
  struct BlueprintBinding {
    const Blueprint* definition = nullptr;
    int state_index = -1;
  };

  struct InitialState {
    uint64_t player_entity_id = 0;
    int player_layer_id = -1;
    std::string player_collider_id;
    AxisAlignedBox player_local_collider;
    Body player_body;
    TileCollisionLookup collision_tiles;
    absl::flat_hash_map<uint64_t, Transform> transforms;
    absl::flat_hash_map<uint64_t, Motion> motions;
    absl::flat_hash_map<uint64_t, PlayerControllerState> player_controllers;
    absl::flat_hash_map<uint64_t, BlueprintBinding> blueprint_bindings;
    absl::flat_hash_map<uint64_t, std::string> sprite_ids;
    absl::flat_hash_map<uint64_t, int> frame_indices;
    absl::flat_hash_map<uint64_t, AnimationCursor> animation_cursors;
  };

  RuntimeWorld(const LoadedLevelContent& content, InitialState state);

  const LoadedLevelContent& content_;
  uint64_t player_entity_id_ = 0;
  int player_layer_id_ = -1;
  std::string player_collider_id_;
  AxisAlignedBox player_local_collider_;
  Body player_body_;
  TileCollisionLookup collision_tiles_;
  absl::flat_hash_map<uint64_t, Transform> transforms_;
  absl::flat_hash_map<uint64_t, Motion> motions_;
  absl::flat_hash_map<uint64_t, PlayerControllerState> player_controllers_;
  absl::flat_hash_map<uint64_t, BlueprintBinding> blueprint_bindings_;
  absl::flat_hash_map<uint64_t, std::string> sprite_ids_;
  absl::flat_hash_map<uint64_t, int> frame_indices_;
  absl::flat_hash_map<uint64_t, AnimationCursor> animation_cursors_;
};

}  // namespace zebes
