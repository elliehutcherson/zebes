#include "engine/focus_lite.h"

#include "absl/strings/str_format.h"

namespace zebes {

void FocusLite::Update(const ControllerState *state) {
  // Update the focus state based on the controller input
  if (state->left != KeyState::none) world_position_.x -= 10;
  if (state->right != KeyState::none) world_position_.x += 10;
  if (state->up != KeyState::none) world_position_.y -= 10;
  if (state->down != KeyState::none) world_position_.y += 10;
}

float FocusLite::x_center() const {
  // Return the x center of the focus
  return world_position_.x;
}

float FocusLite::y_center() const {
  // Return the y center of the focus
  return world_position_.y;
}

std::string FocusLite::to_string() const {
  // Return a string representation of the focus
  return absl::StrFormat("FocusLite(x: %.2f, y: %.2f)", world_position_.x,
                         world_position_.y);
}

}  // namespace zebes