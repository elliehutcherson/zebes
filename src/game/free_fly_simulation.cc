#include "game/free_fly_simulation.h"

#include <cmath>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "common/status_macros.h"
#include "engine/camera_controller.h"
#include "engine/scene_types.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<FreeFlySimulation>> FreeFlySimulation::Create(Options options) {
  if (options.input_manager == nullptr) {
    return absl::InvalidArgumentError("Free-fly simulation requires an input manager");
  }
  if (!std::isfinite(options.move_speed) || options.move_speed < 0.0 ||
      !std::isfinite(options.zoom_speed) || options.zoom_speed < 0.0) {
    return absl::InvalidArgumentError("Free-fly simulation speeds must be finite and non-negative");
  }
  RETURN_IF_ERROR(ValidateSceneCamera(options.camera));
  auto simulation = std::unique_ptr<FreeFlySimulation>(new FreeFlySimulation(options));
  RETURN_IF_ERROR(simulation->Init(options));
  return simulation;
}

FreeFlySimulation::FreeFlySimulation(Options options)
    : input_manager_(*options.input_manager), camera_(options.camera) {}

absl::Status FreeFlySimulation::Init(const Options& options) {
  ASSIGN_OR_RETURN(camera_controller_, CameraController::Create({
                                           .camera = &camera_,
                                           .input_manager = &input_manager_,
                                           .move_speed = options.move_speed,
                                           .zoom_speed = options.zoom_speed,
                                           .zoom_range = options.zoom_range,
                                       }));
  return absl::OkStatus();
}

absl::Status FreeFlySimulation::Step(absl::Duration duration) {
  if (duration <= absl::ZeroDuration() || duration == absl::InfiniteDuration()) {
    return absl::InvalidArgumentError("Free-fly simulation step must be finite and positive");
  }
  camera_controller_->Update(absl::ToDoubleSeconds(duration));
  return absl::OkStatus();
}

}  // namespace zebes
