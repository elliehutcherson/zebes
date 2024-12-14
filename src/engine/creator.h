#pragma once

#include <sys/types.h>

#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "camera_interface.h"
#include "config.h"
#include "controller.h"
#include "focus.h"
#include "shape.h"
#include "vector.h"

namespace zebes {

class Creator : public Focus {
 public:
  struct Options {
    const GameConfig *config;
    CameraInterface *camera;
  };
  static absl::StatusOr<std::unique_ptr<Creator>> Create(
      const Options &options);

  float x_center() const override;
  float y_center() const override;
  // Outputs the current state of the creator.
  std::string to_string() const override;
  // Update the position of the creator, and the position of the mouse.
  void Update(const ControllerState *state) override;
  // Render the tile outline for the tile the mouse is currently hovering over.
  void Render();
  // Outputs the current state of the creator to a bmp format.
  absl::Status SaveToBmp(std::string path) const;
  // Load a bmp file into the creator.
  absl::Status LoadFromBmp(std::string path);
  // Save the current game config to a file.
  absl::Status SaveConfig(const GameConfig &config);
  // Load a game config from a file.
  absl::StatusOr<GameConfig> ImportConfig(const std::string &path);

 private:
  Creator(const Options &options);
  // If the mouse is clicked, toggle the state of the tile the
  // mouse is currently hovering over.
  void ToggleTileState();
  // Get full position for the currently selected tile.
  Point GetPosition() const;

  Point window_dimensions_;
  Point world_position_;
  Point mouse_window_position_;
  Point mouse_window_tile_position_;
  Point mouse_world_position_;
  Point mouse_world_tile_position_;
  Shape shape_;

  const GameConfig *config_;
  CameraInterface *camera_;
  absl::flat_hash_map<Point, std::unique_ptr<Shape>, PointHash> tiles_;
};

}  // namespace zebes