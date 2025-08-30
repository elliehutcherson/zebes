#pragma once

#include <variant>

#include "SDL_events.h"
#include "SDL_keycode.h"
#include "SDL_scancode.h"
#include "absl/status/statusor.h"
#include "common/config.h"
#include "common/vector.h"

namespace zebes {

enum class InternalEventType : uint8_t {
  kCreatorSaveConfig = 0,
  kIsMainWindowFocused = 1,
};

struct InternalEvent {
  InternalEventType type;
  std::variant<bool, GameConfig> value;
};

enum KeyState : uint8_t { none = 0, pressed = 1, down = 2, up = 3 };

struct ControllerState {
  struct Modifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
  };
  Modifiers modifiers;

  KeyState left = KeyState::none;
  KeyState right = KeyState::none;
  KeyState up = KeyState::none;
  KeyState down = KeyState::none;
  KeyState a = KeyState::none;
  KeyState b = KeyState::none;
  KeyState x = KeyState::none;
  KeyState y = KeyState::none;
  KeyState l = KeyState::none;
  KeyState r = KeyState::none;
  KeyState player_reset = KeyState::none;
  KeyState game_quit = KeyState::none;
  KeyState enable_frame_by_frame = KeyState::none;
  KeyState advance_frame = KeyState::none;
  Point mouse_position;

  // Creator mode tile functions.
  KeyState tile_next = KeyState::none;
  KeyState tile_previous = KeyState::none;
  KeyState tile_rotate_clockwise = KeyState::none;
  KeyState tile_rotate_counter_clockwise = KeyState::none;
  KeyState tile_toggle = KeyState::none;
  KeyState tile_reset = KeyState::none;

  // Is the main window focused.
  bool is_main_window_focused = true;
};

class Controller {
 public:
  struct Options {
    std::function<void(const GameConfig &config)> save_config;
  };
  // Will return non-okay status if the controller fails to initialize.
  static absl::StatusOr<std::unique_ptr<Controller>> Create(Options options);

  ~Controller() = default;
  // Handle internal events, this could effect how external events are handled.
  void HandleInternalEvents();
  // Handle keydown event for specific keys.
  void HandleEvent(const SDL_Event *event);
  // Update controller state.
  void Update();
  // Clear keyboard state.
  void Clear();
  // If frame by frame is enabled, check if the user has
  // requested the next frame.
  bool ShouldUpdate();
  // Get controller buttons.
  const ControllerState *GetState() const;
  // Add an internal event to the controller. Internal events
  // will be handled before external events.
  void AddInternalEvent(InternalEvent event) {
    internal_events_.push_back(event);
  }

 private:
  // Constructor.
  Controller(Options options);
  // The pointer returned is a pointer to an internal SDL array.
  // It will be valid for the whole lifetime of the application
  // and should not be freed by the caller.
  const uint8_t *key_states_ = nullptr;
  // Get inputs and translate them into applicable buttons/events.
  void UpdateState(SDL_Keycode code, uint8_t value);
  // Buttons to be read by multiple objects.
  ControllerState state_;
  // Internal events to handle.
  std::vector<InternalEvent> internal_events_;
  // All of the scancodes that we care about.
  std::vector<SDL_Scancode> scan_codes_{
      SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_LCTRL,
      SDL_SCANCODE_RCTRL,  SDL_SCANCODE_LALT,   SDL_SCANCODE_RALT,
      SDL_SCANCODE_ESCAPE, SDL_SCANCODE_0,      SDL_SCANCODE_1,
      SDL_SCANCODE_SPACE,  SDL_SCANCODE_UP,     SDL_SCANCODE_LEFT,
      SDL_SCANCODE_RIGHT,  SDL_SCANCODE_DOWN,   SDL_SCANCODE_W,
      SDL_SCANCODE_D,      SDL_SCANCODE_S,      SDL_SCANCODE_A,
      SDL_SCANCODE_Q,      SDL_SCANCODE_E};
  // All of the keycodes that we care about.
  std::vector<SDL_KeyCode> key_codes_{
      SDL_KeyCode::SDLK_LSHIFT, SDL_KeyCode::SDLK_RSHIFT,
      SDL_KeyCode::SDLK_LCTRL,  SDL_KeyCode::SDLK_RCTRL,
      SDL_KeyCode::SDLK_LALT,   SDL_KeyCode::SDLK_RALT,
      SDL_KeyCode::SDLK_ESCAPE, SDL_KeyCode::SDLK_0,
      SDL_KeyCode::SDLK_1,      SDL_KeyCode::SDLK_SPACE,
      SDL_KeyCode::SDLK_UP,     SDL_KeyCode::SDLK_LEFT,
      SDL_KeyCode::SDLK_RIGHT,  SDL_KeyCode::SDLK_DOWN,
      SDL_KeyCode::SDLK_w,      SDL_KeyCode::SDLK_d,
      SDL_KeyCode::SDLK_s,      SDL_KeyCode::SDLK_a,
      SDL_KeyCode::SDLK_q,      SDL_KeyCode::SDLK_e};

  std::function<void(const GameConfig &config)> save_config_;
};

}  // namespace zebes