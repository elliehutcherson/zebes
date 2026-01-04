#include "engine/input_manager.h"

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "SDL.h"
#include "SDL_scancode.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/sdl_wrapper.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<InputManager>> InputManager::Create(Options options) {
  if (options.sdl_wrapper == nullptr) {
    return absl::InvalidArgumentError("sdl_wrapper cannot be null");
  }
  // We use 'new' and wrap it because std::make_unique cannot access
  // the private constructor.
  return std::unique_ptr<InputManager>(new InputManager(*options.sdl_wrapper));
}

InputManager::InputManager(SdlWrapper& sdl_wrapper) : sdl_wrapper_(sdl_wrapper) {
  curr_keyboard_state_.resize(SDL_NUM_SCANCODES, 0);
  prev_keyboard_state_.resize(SDL_NUM_SCANCODES, 0);
}

void InputManager::BindAction(absl::string_view action_name, SDL_Scancode key) {
  action_bindings_[action_name].push_back(key);
}

void InputManager::Update() {
  // 1. Copy current state to previous state (to detect edges)
  prev_keyboard_state_ = curr_keyboard_state_;

  // 2. Poll SDL Events using the injected wrapper
  SDL_Event event;
  while (sdl_wrapper_.PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      quit_requested_ = true;
    }
    // Handle other events here
  }

  // 3. Update current state from SDL hardware snapshot
  const uint8_t* state = sdl_wrapper_.GetKeyboardState(nullptr);

  if (curr_keyboard_state_.size() < SDL_NUM_SCANCODES) {
    curr_keyboard_state_.resize(SDL_NUM_SCANCODES);
    prev_keyboard_state_.resize(SDL_NUM_SCANCODES);
  }

  std::memcpy(curr_keyboard_state_.data(), state, SDL_NUM_SCANCODES);
}

bool InputManager::IsActionActive(absl::string_view action_name) const {
  auto it = action_bindings_.find(action_name);
  if (it == action_bindings_.end()) {
    return false;
  }
  const std::vector<SDL_Scancode>& scancodes = it->second;

  for (SDL_Scancode scancode : scancodes) {
    if (curr_keyboard_state_[scancode]) {
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
  const std::vector<SDL_Scancode>& scancodes = it->second;

  for (SDL_Scancode scancode : scancodes) {
    if (curr_keyboard_state_[scancode] && !prev_keyboard_state_[scancode]) {
      return true;
    }
  }
  return false;
}

bool InputManager::QuitRequested() const { return quit_requested_; }

}  // namespace zebes
