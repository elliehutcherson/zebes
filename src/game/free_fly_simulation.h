#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "engine/camera_controller.h"
#include "engine/input_manager_interface.h"
#include "game/game_engine.h"
#include "objects/camera.h"

namespace zebes {

// M1's simulation: a camera controlled by held input at a fixed timestep.
// InputManager is sampled once by the main loop and must outlive this object.
class FreeFlySimulation final : public GameSimulation {
 public:
  struct Options {
    IInputManager* input_manager = nullptr;
    Camera camera;
    double move_speed = 480.0;
    double zoom_speed = 1.0;
    CameraZoomRange zoom_range{.minimum = 0.5, .maximum = 2.0};
  };

  static absl::StatusOr<std::unique_ptr<FreeFlySimulation>> Create(Options options);

  absl::Status Step(absl::Duration duration) override;

  const Camera& camera() const { return camera_; }

 private:
  explicit FreeFlySimulation(Options options);

  absl::Status Init(const Options& options);

  IInputManager& input_manager_;
  Camera camera_;
  std::unique_ptr<CameraController> camera_controller_;
};

}  // namespace zebes
