#include "game/player_simulation.h"

#include <cmath>
#include <memory>
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

absl::StatusOr<std::unique_ptr<PlayerSimulation>> PlayerSimulation::Create(Options options) {
  if (options.input_manager == nullptr) {
    return absl::InvalidArgumentError("Player simulation requires an input manager");
  }
  if (options.world == nullptr) {
    return absl::InvalidArgumentError("Player simulation requires a runtime world");
  }
  RETURN_IF_ERROR(options.movement.Validate());
  RETURN_IF_ERROR(ValidateSceneCamera(options.camera));
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
  world_->AdvanceAnimations();
  const Transform* player = world_->FindTransform(world_->player_entity_id());
  if (player == nullptr) {
    return absl::FailedPreconditionError("Player simulation lost the player transform");
  }
  camera_.position = player->position;
  return ValidateSceneCamera(camera_);
}

}  // namespace zebes
