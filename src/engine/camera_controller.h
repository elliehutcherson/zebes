#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "engine/input_manager_interface.h"
#include "objects/camera.h"

namespace zebes {

class CameraController {
 public:
  struct Options {
    Camera* camera;
    IInputManager* input_manager;
    double move_speed = 0;
    double zoom_speed = 0;
  };

  static absl::StatusOr<std::unique_ptr<CameraController>> Create(Options options);

  void Update(double delta_time);

 private:
  CameraController(Options options);

  Camera& camera_;
  IInputManager& input_manager_;
  double move_speed_ = 0;
  double zoom_speed_ = 0;
};

}  // namespace zebes