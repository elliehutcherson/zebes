#pragma once

#include <vector>

#include "SDL_render.h"

#include "imgui.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/container/flat_hash_set.h"

#include "engine/config.h"
#include "engine/controller.h"
#include "engine/focus.h"

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

  // Inject events to the controller, like if ImGui is capturing the mouse.
  void InjectEvents();

  void Update();

  void Render();

private:
  struct Scene {
    std::string name;
    int index = 0;
    bool is_active = false;
  };

  Hud(const GameConfig *config, const Focus *focus, Controller *controller);

  absl::Status Init(SDL_Window *window, SDL_Renderer *renderer);

  void RenderCreatorMode();
  void RenderPlayerMode();
  void RenderWindowConfig();
  void RenderBoundaryConfig();
  void RenderTileConfig();
  void RenderCollisionConfig();
  void RenderSceneWindow(int index);

  const GameConfig *config_;
  const Focus *focus_;
  Controller *controller_;

  ImGuiIO *imgui_io_;
  ImGuiContext *imgui_context_;
  ImVec4 clear_color_;

  // Settings for window configuration.
  WindowConfig hud_window_config_;
  BoundaryConfig hud_boundary_config_;
  TileConfig hud_tile_config_;
  CollisionConfig hud_collision_config_;

  // Path to save creator state.
  char creator_save_path_[4096] = "";
  char creator_import_path_[4096] = "";

  // If the log size hasn't changed, we don't need to update the log.
  int log_size_ = 0;

  // If creating scenes, store them here.
  std::vector<Scene> scenes_;
  absl::flat_hash_set<int> removed_scenes_;
};

} // namespace zebes