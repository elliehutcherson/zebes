#include "creator.h"

#include <cmath>
#include <memory>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"

#include "absl/status/statusor.h"
#include "engine/bitmap.h"
#include "engine/camera.h"
#include "engine/config.h"
#include "engine/controller.h"
#include "engine/shape.h"
#include "engine/util.h"
#include "engine/vector.h"

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

  if (!state->creator_save_path.empty()) {
    absl::Status result = SaveToBmp(state->creator_save_path);
    if (!result.ok()) {
      LOG(ERROR) << absl::StrFormat(
          "%s, failed to save state, error: %s, path: %s", __func__,
          result.message(), state->creator_save_path);
    } else {
      LOG(INFO) << absl::StrFormat("%s, successfully saved state, path: %s",
                                   __func__, state->creator_save_path);
    }
  }

  if (!state->creator_import_path.empty()) {
    absl::Status result = LoadFromBmp(state->creator_import_path);
    if (!result.ok()) {
      LOG(ERROR) << absl::StrFormat(
          "%s, failed to load state, error: %s, path: %s", __func__,
          result.message(), state->creator_import_path);
    } else {
      LOG(INFO) << absl::StrFormat("%s, successfully loaded state, path: %s",
                                   __func__, state->creator_import_path);
    }
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
      LOG(ERROR) << absl::StrFormat(
          "%s, shape render failed, type: %s, error: %s", __func__,
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
      LOG(ERROR) << absl::StrFormat(
          "%s, shape render failed, type: %s, error: %s", __func__,
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

absl::Status Creator::SaveToBmp(std::string path) const {
  Bitmap bitmap(config_->boundaries.x_max, config_->boundaries.y_max);
  for (const auto &[point, shape] : tiles_) {
    Shape::State state = shape->state();
    LOG(INFO) << __func__ << point.to_string();
    absl::Status result = bitmap.Set(point.x_floor(), point.y_floor(),
                                     state.raw_eight[0], state.raw_eight[1], 0);
    if (!result.ok())
      return result;
  }
  return bitmap.SaveToBmp(path);
}

absl::Status Creator::LoadFromBmp(std::string path) {
  tiles_.clear();
  absl::StatusOr<Bitmap> loaded_bitmap = Bitmap::LoadFromBmp(path);
  if (!loaded_bitmap.ok())
    return loaded_bitmap.status();

  Shape::State state;
  for (int x = 0; x < loaded_bitmap->width(); x++) {
    for (int y = 0; y < loaded_bitmap->height(); y++) {
      uint8_t unused = 0;
      absl::Status result = loaded_bitmap->Get(x, y, &state.raw_eight[0],
                                               &state.raw_eight[1], &unused);

      if (!result.ok())
        return result;

      if (state.type == ShapeType::kNone)
        continue;

      Point point =
          Point{.x = static_cast<double>(x), .y = static_cast<double>(y)};
      tiles_[point] = std::unique_ptr<Shape>(new Shape(state));
      Point world_position = Point{
          .x = point.x * config_->tiles.render_width,
          .y = point.y * config_->tiles.render_height};
      tiles_[point]->set_position(world_position);
    }
  }
  return absl::OkStatus();
}

} // namespace zebes
