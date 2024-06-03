#include "engine/controller.h"

#include "SDL_events.h"
#include "SDL_keycode.h"
#include "SDL_mouse.h"

#include "absl/log/log.h"

#include "engine/config.h"

namespace zebes {

Controller::Controller(const GameConfig *config) : config_(config) {}

void Controller::HandleInternalEvents() {
  for (InternalEvent &event : internal_events_) {
    switch (event.type) {
    case InternalEventType::kCreatorSavePath:
      state_.creator_save_path = std::get<std::string>(event.value);
      break;
    case InternalEventType::kCreatorImportPath:
      state_.creator_import_path = std::get<std::string>(event.value);
      break;
    case InternalEventType::kIsMainWindowFocused:
      state_.is_main_window_focused = std::get<bool>(event.value);
      break;
    }
  }
  internal_events_.clear();
}

void Controller::HandleEvent(const SDL_Event *event) {
  if (event->type == SDL_QUIT) {
    UpdateState(SDL_KeyCode::SDLK_ESCAPE, KeyState::pressed);
  }

  if (!state_.is_main_window_focused) {
    return;
  }

  if (event->type == SDL_MOUSEMOTION) {
    int x, y;
    SDL_GetMouseState(&x, &y);
    state_.mouse_position =
        Point{.x = static_cast<double>(x), .y = static_cast<double>(y)};
  } else if (event->type == SDL_MOUSEBUTTONDOWN) {
    if (event->button.button == SDL_BUTTON_LEFT) {
      state_.tile_toggle = KeyState::pressed;
    }
  } else if (event->type == SDL_KEYDOWN) {
    KeyState key_state = event->key.repeat ? KeyState::pressed : KeyState::down;
    UpdateState(event->key.keysym.sym, key_state);
  }
}

void Controller::UpdateState(SDL_Keycode code, uint8_t value) {
  KeyState new_state = static_cast<KeyState>(value);
  std::vector<KeyState *> old_states;
  switch (code) {
  case SDL_KeyCode::SDLK_LSHIFT:
  case SDL_KeyCode::SDLK_RSHIFT:
    state_.modifiers.shift = new_state != KeyState::none;
    break;
  case SDL_KeyCode::SDLK_LCTRL:
  case SDL_KeyCode::SDLK_RCTRL:
    state_.modifiers.ctrl = new_state != KeyState::none;
    break;
  case SDL_KeyCode::SDLK_LALT:
  case SDL_KeyCode::SDLK_RALT:
    state_.modifiers.alt = new_state != KeyState::none;
    break;
  case SDL_KeyCode::SDLK_ESCAPE:
    old_states.push_back(&state_.game_quit);
    break;
  case SDL_KeyCode::SDLK_0:
    old_states.push_back(&state_.player_reset);
    break;
  case SDL_KeyCode::SDLK_1:
    old_states.push_back(&state_.enable_frame_by_frame);
    break;
  case SDL_KeyCode::SDLK_SPACE:
    old_states.push_back(&state_.advance_frame);
    if (state_.modifiers.shift) {
      old_states.push_back(&state_.tile_previous);
    } else {
      old_states.push_back(&state_.tile_next);
    }
    break;
  case SDL_KeyCode::SDLK_UP:
    old_states.push_back(&state_.up);
    break;
  case SDL_KeyCode::SDLK_LEFT:
    old_states.push_back(&state_.left);
    break;
  case SDL_KeyCode::SDLK_RIGHT:
    old_states.push_back(&state_.right);
    break;
  case SDL_KeyCode::SDLK_DOWN:
    old_states.push_back(&state_.down);
    break;
  case SDL_KeyCode::SDLK_w:
    old_states.push_back(&state_.y);
    break;
  case SDL_KeyCode::SDLK_d:
    old_states.push_back(&state_.b);
    break;
  case SDL_KeyCode::SDLK_s:
    old_states.push_back(&state_.a);
    break;
  case SDL_KeyCode::SDLK_a:
    old_states.push_back(&state_.x);
    break;
  case SDL_KeyCode::SDLK_q:
    old_states.push_back(&state_.l);
    break;
  case SDL_KeyCode::SDLK_e:
    old_states.push_back(&state_.r);
    break;
  case SDL_KeyCode::SDLK_r:
    if (state_.modifiers.shift) {
      old_states.push_back(&state_.tile_rotate_counter_clockwise);
    } else {
      old_states.push_back(&state_.tile_rotate_clockwise);
    }
    break;
  default:
    break;
  }
  for (KeyState *old_state : old_states) {
    *old_state = std::max(*old_state, new_state);
  }
}

void Controller::Update() {
  if (!state_.is_main_window_focused) {
    return;
  }

  key_states_ = SDL_GetKeyboardState(NULL);
  for (int i = 0; i < scan_codes_.size(); ++i) {
    UpdateState(key_codes_[i], key_states_[scan_codes_[i]]);
  }
}

void Controller::Clear() {
  Point mouse_position = state_.mouse_position;
  state_ = ControllerState();
  state_.mouse_position = mouse_position;
}

const ControllerState *Controller::GetState() const { return &state_; }

} // namespace zebes
