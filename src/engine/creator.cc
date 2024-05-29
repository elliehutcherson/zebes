#include "creator.h"

#include <cmath>
#include <memory>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "bitmap.h"
#include "camera.h"
#include "config.h"
#include "controller.h"
#include "shape.h"
#include "util.h"
#include "vector.h"

namespace zebes {

Creator::Creator(const GameConfig *config, Camera *camera)
    : config_(config), camera_(camera) {
  Shape::set_render_width(config_->tiles.render_width);
  Shape::set_render_height(config_->tiles.render_height);
};

void Creator::Update(const ControllerState *state) {
  if (state->left != KeyState::none)
    world_position_.x -= 10;
  if (state->right != KeyState::none)
    world_position_.x += 10;
  if (state->up != KeyState::none)
    world_position_.y -= 10;
  if (state->down != KeyState::none)
    world_position_.y += 10;
  if (state->tile_next == KeyState::down)
    shape_.set_type(Shape::TypePlusOne(shape_.type()));
  if (state->tile_previous == KeyState::down)
    shape_.set_type(Shape::TypeMinusOne(shape_.type()));
  if (state->tile_rotate_clockwise == KeyState::down)
    shape_.set_rotation(Shape::RotationPlusOne(shape_.rotation()));
  if (state->tile_rotate_counter_clockwise == KeyState::down)
    shape_.set_rotation(Shape::RotationMinusOne(shape_.rotation()));

  window_dimensions_ = {static_cast<double>(config_->window.width),
                        static_cast<double>(config_->window.height)};
  mouse_window_position_ = state->mouse_position;
  mouse_world_position_ =
      GetWorldPosition(mouse_window_position_, world_position_,
                       window_dimensions_, config_->boundaries);
  mouse_window_tile_position_ = Point{
      .x = floor(mouse_window_position_.x / config_->tiles.render_width),
      .y = floor(mouse_window_position_.y / config_->tiles.render_height)};
  mouse_world_tile_position_ =
      Point{.x = floor(mouse_world_position_.x / config_->tiles.render_width),
            .y = floor(mouse_world_position_.y / config_->tiles.render_height)};

  if (state->tile_toggle == KeyState::pressed) {
    ToggleTileState();
  }
}

float Creator::x_center() const { return world_position_.x; }

float Creator::y_center() const { return world_position_.y; }

std::string Creator::to_string() const {
  return absl::StrFormat("Position: %s\nMouse Window: %s\nMouse World: %s\n",
                         world_position_.to_string(),
                         mouse_window_position_.to_string(),
                         mouse_world_position_.to_string());
}

Point Creator::GetPosition() const {
  return {.x = mouse_world_tile_position_.x * config_->tiles.render_width,
          .y = mouse_world_tile_position_.y * config_->tiles.render_height};
}

void Creator::Render() {
  auto it = tiles_.find(mouse_world_tile_position_);
  if (it == tiles_.end()) {
    shape_.set_position(GetPosition());
    absl::Status result = camera_->RenderLines(*shape_.polygon()->vertices(),
                                               DrawColor::kColorTile,
                                               /*static_position=*/false);
    if (!result.ok()) {
      std::cerr << absl::StrFormat(
          "%s, shape render failed, type: %s, error: %s\n", __func__,
          Shape::TypeToString(shape_.type()), result.message());

      // Reset shape if render failed.
      shape_ = Shape();
    }
  }

  absl::flat_hash_set<Point, PointHash> failed_renders;
  for (const auto &[point, shape] : tiles_) {
    absl::Status result = camera_->RenderLines(*shape->polygon()->vertices(),
                                               DrawColor::kColorTile,
                                               /*static_position=*/false);
    if (!result.ok()) {
      std::cerr << absl::StrFormat(
          "%s, shape render failed, type: %s, error: %s\n", __func__,
          Shape::TypeToString(shape_.type()), result.message());
     failed_renders.insert(point); 
    }
  }
  // Erase points that failed to render.
  for (const auto &point : failed_renders) {
    tiles_.erase(point);
  }
}

void Creator::ToggleTileState() {
  auto it = tiles_.find(mouse_world_tile_position_);
  if (it != tiles_.end()) {
    tiles_.erase(mouse_world_tile_position_);
    return;
  }
  tiles_[mouse_world_tile_position_] =
      std::unique_ptr<Shape>(new Shape(std::move(shape_)));
  shape_ = Shape();
}

std::string Creator::StateToCsv() const {
  Bitmap bitmap(config_->boundaries.x_max, config_->boundaries.y_max);
  for (const auto &[_, polygon] : tiles_) {
  }
}

} // namespace zebes