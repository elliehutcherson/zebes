#include "platform/sdl/sdl_input_source.h"

#include <array>
#include <utility>

#include "SDL.h"
#include "common/imgui_wrapper.h"
#include "common/sdl_wrapper.h"

namespace zebes {
namespace {

constexpr std::array<std::pair<Key, SDL_Scancode>, 9> kKeyMappings = {{
    {Key::kA, SDL_SCANCODE_A},
    {Key::kD, SDL_SCANCODE_D},
    {Key::kE, SDL_SCANCODE_E},
    {Key::kF, SDL_SCANCODE_F},
    {Key::kQ, SDL_SCANCODE_Q},
    {Key::kS, SDL_SCANCODE_S},
    {Key::kW, SDL_SCANCODE_W},
    {Key::kSpace, SDL_SCANCODE_SPACE},
    {Key::kRight, SDL_SCANCODE_RIGHT},
}};

}  // namespace

SdlInputSource::SdlInputSource(SdlWrapper& sdl, ImGuiWrapper& imgui) : sdl_(sdl), imgui_(imgui) {}

InputSnapshot SdlInputSource::Poll() {
  InputSnapshot snapshot;
  SDL_Event event;
  while (sdl_.PollEvent(&event)) {
    imgui_.ProcessEvent(&event);
    if (event.type == SDL_QUIT ||
        (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
      snapshot.quit_requested = true;
    }
  }

  const uint8_t* keyboard_state = sdl_.GetKeyboardState(nullptr);
  for (const std::pair<Key, SDL_Scancode>& mapping : kKeyMappings) {
    snapshot.SetKeyDown(mapping.first, keyboard_state[mapping.second] != 0);
  }
  return snapshot;
}

}  // namespace zebes
