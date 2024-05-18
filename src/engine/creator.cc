#include "creator.h"

#include <memory>
#include <string>

#include "config.h"
#include "controller.h"

namespace zebes {

Creator::Creator(const GameConfig* config) : 
config_(config) {};

void Creator::Update(const ControllerState* state) {
  if (state->left != KeyState::none) point_.x--;
  if (state->right != KeyState::none) point_.x++; 
  if (state->up != KeyState::none) point_.y--;
  if (state->down != KeyState::none) point_.y++; 
}

float Creator::x_center() const {
  return point_.x;
}

float Creator::y_center() const {
  return point_.x;
}

std::string Creator::to_string() const {
  return "Creator: " + point_.to_string();
}

}  // namespace zebes