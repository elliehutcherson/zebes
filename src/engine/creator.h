#pragma once

#include <memory>
#include <string>

#include "config.h"
#include "controller.h"
#include "engine/camera.h"
#include "engine/polygon.h"
#include "vector.h"
#include "focus.h"

namespace zebes {

class Creator : public Focus {
 public:
  Creator(const GameConfig* config, Camera* camera);

  float x_center() const override;
  float y_center() const override;

  // Update the position of the creator, and the position of the mouse.
  void Update(const ControllerState* state) override;
  // Render the tile outline for the tile the mouse is currently hovering over.
  void Render();
  // Outputs the current state of the creator.
  std::string to_string() const override; 

 private:
  // If the mouse is clicked, toggle the state of the tile the 
  // mouse is currently hovering over.
  void ToggleTileState();
  // Get vertices for the tile the mouse is currently hovering over.
  std::vector<Point> GetTileVertices() const;

  Point window_dimensions_;
  Point world_position_;
  Point mouse_window_position_;
  Point mouse_window_tile_position_;
  Point mouse_world_position_;
  Point mouse_world_tile_position_;

  const GameConfig* config_;
  Camera* camera_;
  absl::flat_hash_map<Point, std::unique_ptr<Polygon>, PointHash> tiles_;
};

}  // namespace zebes