#include "controller.h"

#include "SDL_events.h"
#include "SDL_mouse.h"
#include "config.h"

namespace zebes {

Controller::Controller(const GameConfig* config) : config_(config) {}

void Controller::HandleEvent(const SDL_Event* event) {
  if (event->type == SDL_MOUSEMOTION) {
    int x, y;
    SDL_GetMouseState(&x, &y);
    state_.mouse_position = Point {.x = static_cast<double>(x), .y = static_cast<double>(y)};
  } else if (event->type == SDL_MOUSEBUTTONDOWN) {
    if (event->button.button == SDL_BUTTON_LEFT) {
      state_.left_click = KeyState::pressed;
    }
  } else if (event->type == SDL_QUIT) {
    UpdateState(SDL_KeyCode::SDLK_ESCAPE, KeyState::pressed);
  } else if (event->type == SDL_KEYDOWN) {
    KeyState key_state = event->key.repeat ? KeyState::pressed : KeyState::down;
    UpdateState(event->key.keysym.sym, key_state);
  } 
}

void Controller::UpdateState(SDL_Keycode code, uint8_t value, bool overwrite) {
  KeyState key_state = static_cast<KeyState>(value);
  switch (code) {
    case SDL_KeyCode::SDLK_ESCAPE:
      if (overwrite || state_.game_quit <  key_state) state_.game_quit = key_state;
      break;
    case SDL_KeyCode::SDLK_0:
      if (overwrite || state_.player_reset <  key_state) state_.player_reset = key_state;
      break;
    case SDL_KeyCode::SDLK_1:
      if (overwrite || state_.enable_frame_by_frame <  key_state) state_.enable_frame_by_frame = key_state;
      break;
    case SDL_KeyCode::SDLK_SPACE:
      if (overwrite || state_.advance_frame <  key_state) state_.advance_frame = key_state;
      break;
    case SDL_KeyCode::SDLK_UP:
      if (overwrite || state_.up <  key_state) state_.up = key_state;
      break;
    case SDL_KeyCode::SDLK_LEFT:
      if (overwrite || state_.left <  key_state) state_.left = key_state;
      break;
    case SDL_KeyCode::SDLK_RIGHT:
      if (overwrite || state_.right <  key_state) state_.right = key_state;
      break;
    case SDL_KeyCode::SDLK_DOWN:
      if (overwrite || state_.down <  key_state) state_.down = key_state;
      break;
    case SDL_KeyCode::SDLK_w:
      if (overwrite || state_.y <  key_state) state_.y = key_state;
      break;
    case SDL_KeyCode::SDLK_d:
      if (overwrite || state_.b <  key_state) state_.b = key_state;
      break;
    case SDL_KeyCode::SDLK_s:
      if (overwrite || state_.a <  key_state) state_.a = key_state;
      break;
    case SDL_KeyCode::SDLK_a:
      if (overwrite || state_.x <  key_state) state_.x = key_state;
      break;
    case SDL_KeyCode::SDLK_q:
      if (overwrite || state_.l <  key_state) state_.l = key_state;
      break;
    case SDL_KeyCode::SDLK_e:
      if (overwrite || state_.r <  key_state) state_.r = key_state;
      break;
    default:
      break;
  }
}

void Controller::Update() {
  key_states_ = SDL_GetKeyboardState(NULL);
  for (int i = 0; i < scan_codes_.size(); ++i) {
    UpdateState(key_codes_[i], key_states_[scan_codes_[i]]);
  }
}

void Controller::Clear() {
  for (int i = 0; i < scan_codes_.size(); ++i) {
    UpdateState(key_codes_[i], KeyState::none, /*overwrite=*/true);
  }
  state_.left_click = KeyState::none;
  state_.right_click = KeyState::none;
}

const ControllerState* Controller::GetState() const {
  return &state_;
}

}  // namespace zebes