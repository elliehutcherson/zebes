#include "game/player_simulation.h"

#include <array>
#include <cmath>
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

constexpr std::array kPlayerAnimationStateKeys = {
    kIdleLeft, kIdleRight, kRunLeft, kRunRight, kAirborneLeft, kAirborneRight,
};

std::string_view SelectPlayerAnimationState(const PlayerControllerState& controller,
                                            const Motion& motion) {
  if (!controller.grounded) {
    return controller.facing == PlayerFacing::kLeft ? kAirborneLeft : kAirborneRight;
  }
  if (motion.velocity.x != 0.0) {
    return controller.facing == PlayerFacing::kLeft ? kRunLeft : kRunRight;
  }
  return controller.facing == PlayerFacing::kLeft ? kIdleLeft : kIdleRight;
}

absl::Status ValidatePlayerAnimationContract(const RuntimeWorld& world) {
  for (std::string_view state_key : kPlayerAnimationStateKeys) {
    RETURN_IF_ERROR(world.ValidateEntityBlueprintState(world.player_entity_id(), state_key));
  }
  return absl::OkStatus();
}

absl::Status UpdatePlayerAnimation(RuntimeWorld& world) {
  const uint64_t player_id = world.player_entity_id();
  const PlayerControllerState* controller = world.FindPlayerController(player_id);
  const Motion* motion = world.FindMotion(player_id);
  if (controller == nullptr || motion == nullptr) {
    return absl::FailedPreconditionError("Player animation requires controller and motion state");
  }
  return world.SetEntityBlueprintState(player_id, SelectPlayerAnimationState(*controller, *motion));
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
  RETURN_IF_ERROR(ValidatePlayerAnimationContract(*options.world));
  RETURN_IF_ERROR(UpdatePlayerAnimation(*options.world));
  const Transform* player = options.world->FindTransform(options.world->player_entity_id());
  if (player == nullptr) {
    return absl::FailedPreconditionError("Player simulation cannot resolve the player transform");
  }
  options.camera.position = player->position;
  RETURN_IF_ERROR(ValidateSceneCamera(options.camera));
  return std::unique_ptr<PlayerSimulation>(new PlayerSimulation(std::move(options)));
}

PlayerSimulation::PlayerSimulation(Options options)
    : input_manager_(*options.input_manager),
      world_(std::move(options.world)),
      camera_(options.camera),
      movement_(options.movement) {}

absl::Status PlayerSimulation::Step(absl::Duration duration) {
  if (duration <= absl::ZeroDuration() || duration == absl::InfiniteDuration()) {
    return absl::InvalidArgumentError("Player simulation step must be finite and positive");
  }
  const double delta_seconds = absl::ToDoubleSeconds(duration);
  if (!std::isfinite(delta_seconds)) {
    return absl::InvalidArgumentError("Player simulation step must be finite and positive");
  }
  RETURN_IF_ERROR(world_->StepPlayer(input_manager_.CurrentSnapshot(), delta_seconds, movement_));
  RETURN_IF_ERROR(UpdatePlayerAnimation(*world_));
  world_->AdvanceAnimations();
  const Transform* player = world_->FindTransform(world_->player_entity_id());
  if (player == nullptr) {
    return absl::FailedPreconditionError("Player simulation lost the player transform");
  }
  camera_.position = player->position;
  return ValidateSceneCamera(camera_);
}

}  // namespace zebes
