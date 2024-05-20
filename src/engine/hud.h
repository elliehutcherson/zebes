#pragma once

#include "SDL_render.h"
#include "imgui.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "config.h"
#include "focus.h"

namespace zebes {

class Hud {
public:
  // Create a new HUD object.
  static absl::StatusOr<std::unique_ptr<Hud>> Create(const GameConfig *config,
                                                     const Focus *focus,
                                                     SDL_Window *window,
                                                     SDL_Renderer *renderer);

  ~Hud() = default;
  // Render the text.
  void Render();

private:
  Hud(const GameConfig *config, const Focus *focus);
  // Initialize IMGUI and other elements.
  absl::Status Init(SDL_Window *window, SDL_Renderer *renderer);
  // Global config.
  const GameConfig *config_;
  // IMGUI Related items.
  ImGuiIO *imgui_io_;
  ImGuiContext *imgui_context_;
  ImVec4 clear_color_;
  // Items related to getting information to display.
  const Focus *focus_;
  // Path to save creator state.
  char save_path_[4096] = "";
};

} // namespace zebes