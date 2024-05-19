#pragma once 

#include "SDL_events.h"

#include "config.h"
#include "vector.h"

namespace zebes {

enum KeyState : uint8_t {
  none = 0,
  pressed = 1,
  down = 2,
  up = 3
};

struct ControllerState {
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
};

class Controller {
  public:
    // Constructor & Destructor.
    Controller(const GameConfig* config);
    ~Controller() = default;
    // Handle keydown event for specific keys.
    void HandleEvent(const SDL_Event* event);
    // Update controller state.
    void Update();
    // Clear keyboard state.
    void Clear();
    // If frame by frame is enabled, check if the user has 
    // requested the next frame.
    bool ShouldUpdate();
    // Get controller buttons.
    const ControllerState* GetState() const;

  private:
    // The pointer returned is a pointer to an internal SDL array. 
    // It will be valid for the whole lifetime of the application 
    // and should not be freed by the caller.
    const uint8_t* key_states_ = nullptr;
    // Get inputs and translate them into applicable buttons/events.
    void UpdateState(SDL_Keycode code, uint8_t value, bool overwrite = false);
    // Game config.
    const GameConfig* config_;
    // Buttons to be read by multiple objects.
    ControllerState state_;
    // All of the scancodes that we care about.
    std::vector<SDL_Scancode> scan_codes_ {
      SDL_SCANCODE_ESCAPE, SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_SPACE, 
      SDL_SCANCODE_UP, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN,
      SDL_SCANCODE_W, SDL_SCANCODE_D, SDL_SCANCODE_S, SDL_SCANCODE_A, 
      SDL_SCANCODE_Q, SDL_SCANCODE_E};
    // All of the keycodes that we care about.
    std::vector<SDL_KeyCode> key_codes_ {
      SDL_KeyCode::SDLK_ESCAPE, SDL_KeyCode::SDLK_0, SDL_KeyCode::SDLK_1, SDL_KeyCode::SDLK_SPACE, 
      SDL_KeyCode::SDLK_UP, SDL_KeyCode::SDLK_LEFT, SDL_KeyCode::SDLK_RIGHT, SDL_KeyCode::SDLK_DOWN,
      SDL_KeyCode::SDLK_w, SDL_KeyCode::SDLK_d, SDL_KeyCode::SDLK_s, SDL_KeyCode::SDLK_a, 
      SDL_KeyCode::SDLK_q, SDL_KeyCode::SDLK_e};
};

}  // namespace zebes