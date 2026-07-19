#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "engine/input_manager_interface.h"
#include "objects/camera.h"

namespace zebes {

class CameraController {
 public:
  struct Options {
    Camera* camera = nullptr;
    IInputManager* input_manager = nullptr;
    double move_speed = 0;
    double zoom_speed = 0;
    CameraZoomRange zoom_range{.minimum = 0.1, .maximum = 5.0};
  };

  static absl::StatusOr<std::unique_ptr<CameraController>> Create(Options options);

  void Update(double delta_time);

 private:
  CameraController(Options options);

  Camera& camera_;
  IInputManager& input_manager_;
  double move_speed_ = 0;
  double zoom_speed_ = 0;
  CameraZoomRange zoom_range_;
};

}  // namespace zebes
