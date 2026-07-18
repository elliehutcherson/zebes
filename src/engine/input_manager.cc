#include "engine/input_manager.h"

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "engine/input_types.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<InputManager>> InputManager::Create(Options options) {
  if (options.input_source == nullptr) {
    return absl::InvalidArgumentError("input_source cannot be null");
  }
  // We use 'new' and wrap it because std::make_unique cannot access
  // the private constructor.
  return std::unique_ptr<InputManager>(new InputManager(*options.input_source));
}

InputManager::InputManager(InputSource& input_source) : input_source_(input_source) {}

void InputManager::BindAction(absl::string_view action_name, Key key) {
  action_bindings_[action_name].push_back(key);
}

void InputManager::Update() {
  previous_snapshot_ = current_snapshot_;
  current_snapshot_ = input_source_.Poll();
  quit_requested_ = quit_requested_ || current_snapshot_.quit_requested;
}

bool InputManager::IsActionActive(absl::string_view action_name) const {
  auto it = action_bindings_.find(action_name);
  if (it == action_bindings_.end()) {
    return false;
  }
  for (Key key : it->second) {
    if (current_snapshot_.IsKeyDown(key)) {
      return true;
    }
  }
  return false;
}

bool InputManager::IsActionJustPressed(absl::string_view action_name) const {
  auto it = action_bindings_.find(action_name);
  if (it == action_bindings_.end()) {
    return false;
  }
  for (Key key : it->second) {
    if (current_snapshot_.IsKeyDown(key) && !previous_snapshot_.IsKeyDown(key)) {
      return true;
    }
  }
  return false;
}

bool InputManager::QuitRequested() const { return quit_requested_; }

}  // namespace zebes
