#pragma once

#include <cstring>  // Required for memcpy
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "SDL.h"
#include "SDL_scancode.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "engine/input_manager_interface.h"

namespace zebes {

class SdlWrapper;

class InputManager : public IInputManager {
 public:
  // Alias for the poll event function signature: int (*)(SDL_Event*)
  using PollEventCallback = std::function<int(SDL_Event*)>;

  struct Options {
    // SdlWrapper dependency. Must not be null.
    // The InputManager does not own this dependency.
    SdlWrapper* sdl_wrapper = nullptr;
  };

  // Factory function
  static absl::StatusOr<std::unique_ptr<InputManager>> Create(Options options);

  // Delete copy and move constructors/operators
  InputManager(const InputManager&) = delete;
  InputManager& operator=(const InputManager&) = delete;

  ~InputManager() override = default;

  // --- 1. Registration API ---
  void BindAction(absl::string_view action_name, SDL_Scancode key) override;

  // --- 2. Interception Loop ---
  void Update() override;

  // --- 3. Query API ---
  bool IsActionActive(absl::string_view action_name) const override;

  bool IsActionJustPressed(absl::string_view action_name) const override;

  bool QuitRequested() const override;

 private:
  // Private constructor
  explicit InputManager(SdlWrapper& sdl_wrapper);

  SdlWrapper& sdl_wrapper_;
  absl::flat_hash_map<std::string, std::vector<SDL_Scancode>> action_bindings_;
  std::vector<uint8_t> curr_keyboard_state_;
  std::vector<uint8_t> prev_keyboard_state_;
  bool quit_requested_ = false;
};

}  // namespace zebes