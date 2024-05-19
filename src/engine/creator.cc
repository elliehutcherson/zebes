#include "creator.h"

#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "controller.h"
#include "camera.h"
#include "engine/vector.h"
#include "util.h"

namespace zebes {

Creator::Creator(const GameConfig* config, Camera* camera) : 
config_(config), camera_(camera) {};

void Creator::Update(const ControllerState* state) {
  if (state->left != KeyState::none) world_position_.x -= 10;
  if (state->right != KeyState::none) world_position_.x += 10; 
  if (state->up != KeyState::none) world_position_.y -= 10;
  if (state->down != KeyState::none) world_position_.y += 10; 

  window_dimensions_ = {static_cast<double>(config_->window.width), static_cast<double>(config_->window.height)};
  mouse_window_position_ = state->mouse_position;
  mouse_world_position_ = GetWorldPosition(
    mouse_window_position_, world_position_, window_dimensions_, config_->boundaries);
  mouse_world_tile_position_ = Point {
    .x = floor(mouse_world_position_.x / config_->tiles.render_width),
    .y = floor(mouse_world_position_.y / config_->tiles.render_height)
  };
  mouse_window_tile_position_ = Point {
    .x = floor(mouse_window_position_.x / config_->tiles.render_width),
    .y = floor(mouse_window_position_.y / config_->tiles.render_height)
  };
}

float Creator::x_center() const {
  return world_position_.x;
}

float Creator::y_center() const {
  return world_position_.y;
}

std::string Creator::to_string() const {
  return absl::StrFormat(
    "Creator: \nPosition: %s\nMouse Window: %s\nMouse World: %s\n", 
    world_position_.to_string(), mouse_window_position_.to_string(), mouse_world_position_.to_string());
}

void Creator::Render() {
  Point point {.x = mouse_world_tile_position_.x * config_->tiles.render_width, .y = mouse_world_tile_position_.y * config_->tiles.render_height};
  std::vector<Point> vertices = {
    point, 
    {point.x + config_->tiles.render_width, point.y}, 
    {point.x + config_->tiles.render_width, point.y + config_->tiles.render_height}, 
    {point.x, point.y + config_->tiles.render_height}};
  camera_->RenderLines(vertices, DrawColor::kColorTile, /*static_position=*/false);
}

}  // namespace zebes