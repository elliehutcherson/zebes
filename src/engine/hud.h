#pragma once

#include "SDL_render.h"
#include "imgui.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "config.h"
#include "controller.h"
#include "focus.h"

namespace zebes {

class Hud {
public:
  struct Options {
    const GameConfig *config;
    const Focus *focus;
    Controller *controller;

    SDL_Window *window;
    SDL_Renderer *renderer;
  };

  static absl::StatusOr<std::unique_ptr<Hud>> Create(Options options);

  ~Hud() = default;

  void Render();

private:
  Hud(const GameConfig *config, const Focus *focus, Controller *controller);

  absl::Status Init(SDL_Window *window, SDL_Renderer *renderer);

  const GameConfig *config_;
  const Focus *focus_;
  Controller *controller_;

  ImGuiIO *imgui_io_;
  ImGuiContext *imgui_context_;
  ImVec4 clear_color_;

  // Path to save creator state.
  char creator_save_path_[4096] = "";
  char creator_import_path_[4096] = "";

  // If the log size hasn't changed, we don't need to update the log.
  int log_size_ = 0;
};

} // namespace zebes