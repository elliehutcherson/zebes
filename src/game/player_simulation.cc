#include "game/player_simulation.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "common/status_macros.h"
#include "engine/input_manager_interface.h"
#include "engine/scene_types.h"
#include "game/runtime_world.h"
#include "objects/camera.h"
#include "objects/transform.h"

namespace zebes {
namespace {

constexpr std::string_view kIdleLeft = "idle-left";
constexpr std::string_view kIdleRight = "idle-right";
constexpr std::string_view kRunLeft = "run-left";
constexpr std::string_view kRunRight = "run-right";
constexpr std::string_view kAirborneLeft = "airborne-left";
constexpr std::string_view kAirborneRight = "airborne-right";

enum class PlayerAnimationState : size_t {
  kIdleLeft,
  kIdleRight,
  kRunLeft,
  kRunRight,
  kAirborneLeft,
  kAirborneRight,
};

constexpr size_t ToIndex(PlayerAnimationState state) { return static_cast<size_t>(state); }

PlayerAnimationState SelectPlayerAnimationState(const PlayerControllerState& controller,
                                                const Motion& motion) {
  if (!controller.grounded) {
    return controller.facing == PlayerFacing::kLeft ? PlayerAnimationState::kAirborneLeft
                                                    : PlayerAnimationState::kAirborneRight;
  }
  if (motion.velocity.x != 0.0) {
    return controller.facing == PlayerFacing::kLeft ? PlayerAnimationState::kRunLeft
                                                    : PlayerAnimationState::kRunRight;
  }
  return controller.facing == PlayerFacing::kLeft ? PlayerAnimationState::kIdleLeft
                                                  : PlayerAnimationState::kIdleRight;
}

using ResolvedPlayerAnimationStates = std::array<RuntimeWorld::ResolvedBlueprintState, 6>;

absl::StatusOr<ResolvedPlayerAnimationStates> ResolvePlayerAnimationStates(
    const RuntimeWorld& world) {
  const uint64_t player_id = world.player_entity_id();
  ASSIGN_OR_RETURN(const RuntimeWorld::ResolvedBlueprintState idle_left,
                   world.ResolveEntityBlueprintState(player_id, kIdleLeft));
  ASSIGN_OR_RETURN(const RuntimeWorld::ResolvedBlueprintState idle_right,
                   world.ResolveEntityBlueprintState(player_id, kIdleRight));
  ASSIGN_OR_RETURN(const RuntimeWorld::ResolvedBlueprintState run_left,
                   world.ResolveEntityBlueprintState(player_id, kRunLeft));
  ASSIGN_OR_RETURN(const RuntimeWorld::ResolvedBlueprintState run_right,
                   world.ResolveEntityBlueprintState(player_id, kRunRight));
  ASSIGN_OR_RETURN(const RuntimeWorld::ResolvedBlueprintState airborne_left,
                   world.ResolveEntityBlueprintState(player_id, kAirborneLeft));
  ASSIGN_OR_RETURN(const RuntimeWorld::ResolvedBlueprintState airborne_right,
                   world.ResolveEntityBlueprintState(player_id, kAirborneRight));
  return ResolvedPlayerAnimationStates{
      idle_left, idle_right, run_left, run_right, airborne_left, airborne_right,
  };
}

absl::Status UpdatePlayerAnimation(RuntimeWorld& world,
                                   const ResolvedPlayerAnimationStates& states) {
  const uint64_t player_id = world.player_entity_id();
  const PlayerControllerState* controller = world.FindPlayerController(player_id);
  const Motion* motion = world.FindMotion(player_id);
  if (controller == nullptr || motion == nullptr) {
    return absl::FailedPreconditionError("Player animation requires controller and motion state");
  }
  const PlayerAnimationState selected = SelectPlayerAnimationState(*controller, *motion);
  return world.SetEntityBlueprintState(player_id, states[ToIndex(selected)]);
}

}  // namespace

absl::StatusOr<std::unique_ptr<PlayerSimulation>> PlayerSimulation::Create(Options options) {
  if (options.input_manager == nullptr) {
    return absl::InvalidArgumentError("Player simulation requires an input manager");
  }
  if (options.world == nullptr) {
    return absl::InvalidArgumentError("Player simulation requires a runtime world");
  }
  RETURN_IF_ERROR(options.movement.Validate());
  RETURN_IF_ERROR(ValidateSceneCamera(options.camera));
  ASSIGN_OR_RETURN(AnimationStates animation_states, ResolvePlayerAnimationStates(*options.world));
  RETURN_IF_ERROR(UpdatePlayerAnimation(*options.world, animation_states));
  const Transform* player = options.world->FindTransform(options.world->player_entity_id());
  if (player == nullptr) {
    return absl::FailedPreconditionError("Player simulation cannot resolve the player transform");
  }
  options.camera.position = player->position;
  RETURN_IF_ERROR(ValidateSceneCamera(options.camera));
  return std::unique_ptr<PlayerSimulation>(
      new PlayerSimulation(std::move(options), std::move(animation_states)));
}

PlayerSimulation::PlayerSimulation(Options options, AnimationStates animation_states)
    : input_manager_(*options.input_manager),
      world_(std::move(options.world)),
      camera_(options.camera),
      movement_(options.movement),
      animation_states_(std::move(animation_states)) {}

absl::Status PlayerSimulation::Step(absl::Duration duration) {
  if (duration <= absl::ZeroDuration() || duration == absl::InfiniteDuration()) {
    return absl::InvalidArgumentError("Player simulation step must be finite and positive");
  }
  const double delta_seconds = absl::ToDoubleSeconds(duration);
  if (!std::isfinite(delta_seconds)) {
    return absl::InvalidArgumentError("Player simulation step must be finite and positive");
  }
  RETURN_IF_ERROR(world_->StepPlayer(input_manager_.CurrentSnapshot(), delta_seconds, movement_));
  RETURN_IF_ERROR(UpdatePlayerAnimation(*world_, animation_states_));
  world_->AdvanceAnimations();
  const Transform* player = world_->FindTransform(world_->player_entity_id());
  if (player == nullptr) {
    return absl::FailedPreconditionError("Player simulation lost the player transform");
  }
  camera_.position = player->position;
  return ValidateSceneCamera(camera_);
}

}  // namespace zebes
