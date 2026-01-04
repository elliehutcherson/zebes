#include "engine/camera_controller.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "objects/camera.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<CameraController>> CameraController::Create(Options options) {
  if (options.camera == nullptr) {
    return absl::InvalidArgumentError("Camera can not be null!!");
  }
  if (options.input_manager == nullptr) {
    return absl::InvalidArgumentError("InputManager can not be null!!");
  }
  return absl::WrapUnique(new CameraController(std::move(options)));
}

CameraController::CameraController(Options options)
    : camera_(*options.camera),
      input_manager_(*options.input_manager),
      move_speed_(options.move_speed),
      zoom_speed_(options.zoom_speed) {
  // Register Inputs (Separation of Mapping vs Logic)
  // You can load these from a file in a real engine
  input_manager_.BindAction("PanUp", SDL_SCANCODE_W);
  input_manager_.BindAction("PanDown", SDL_SCANCODE_S);
  input_manager_.BindAction("PanLeft", SDL_SCANCODE_A);
  input_manager_.BindAction("PanRight", SDL_SCANCODE_D);
  input_manager_.BindAction("ZoomIn", SDL_SCANCODE_E);
  input_manager_.BindAction("ZoomOut", SDL_SCANCODE_Q);
}

void CameraController::SetCamera(Camera& camera) { camera_ = camera; }

void CameraController::Update(double delta_time) {
  Vec movement = {0, 0};

  // 1. Handle Panning
  if (input_manager_.IsActionActive("PanUp")) movement.y -= 1;
  if (input_manager_.IsActionActive("PanDown")) movement.y += 1;
  if (input_manager_.IsActionActive("PanLeft")) movement.x -= 1;
  if (input_manager_.IsActionActive("PanRight")) movement.x += 1;

  // Normalize vector if needed (omitted for brevity) and apply speed
  if (movement.x != 0 || movement.y != 0) {
    // Divide speed by zoom so we move consistent speed relative to screen
    double speed = move_speed_ / camera_.zoom;
    camera_.position.x += movement.x * speed * delta_time;
    camera_.position.y += movement.y * speed * delta_time;
  }

  // 2. Handle Zooming
  if (input_manager_.IsActionActive("ZoomIn")) camera_.zoom += zoom_speed_ * delta_time;
  if (input_manager_.IsActionActive("ZoomOut")) camera_.zoom -= zoom_speed_ * delta_time;

  // Clamp Zoom to sane values
  if (camera_.zoom < 0.1) camera_.zoom = 0.1;
  if (camera_.zoom > 5.0) camera_.zoom = 5.0;
}

}  // namespace zebes
