#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "engine/input_manager_interface.h"
#include "objects/camera.h"

namespace zebes {

// Drives a Camera from keyboard input: WASD pans, Q and E zoom. Bindings are
// registered on the input manager at construction, so two controllers sharing
// one input manager bind the same actions twice and both cameras move.
//
// Borrows the camera and the input manager; both must outlive it. Update()
// expects the input manager to have been polled already, and clamps zoom to
// zoom_range every time, so the camera stays in range even if something else
// wrote a zoom outside it.
class CameraController {
 public:
  struct Options {
    Camera* camera = nullptr;
    IInputManager* input_manager = nullptr;
    double move_speed = 0;
    double zoom_speed = 0;
    CameraZoomRange zoom_range{.minimum = 0.1, .maximum = 5.0};
  };

  // Fails if the camera or input manager is null, or the zoom range is invalid.
  static absl::StatusOr<std::unique_ptr<CameraController>> Create(Options options);

  void Update(double delta_time);

 private:
  explicit CameraController(Options options);

  Camera& camera_;
  IInputManager& input_manager_;
  double move_speed_ = 0;
  double zoom_speed_ = 0;
  CameraZoomRange zoom_range_;
};

}  // namespace zebes
