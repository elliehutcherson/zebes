#pragma once

#include <vector>

#include "SDL_render.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "engine/config.h"
#include "engine/controller.h"
#include "engine/focus.h"
#include "engine/texture_manager.h"
#include "imgui.h"

namespace zebes {

class Hud {
 public:
  struct Options {
    const GameConfig *config;
    const Focus *focus;
    Controller *controller;
    TextureManager *texture_manager;

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
    char save_path[4096] = "";
    char import_path[4096] = "";
  };

  Hud(Options options);

  absl::Status Init(SDL_Window *window, SDL_Renderer *renderer);

  void RenderCreatorMode();
  void RenderPlayerMode();
  void RenderWindowConfig();
  void RenderBoundaryConfig();
  void RenderTileConfig();
  void RenderCollisionConfig();
  void RenderSceneWindowPrimary();
  void RenderSceneWindow(int index);
  void RenderTextureWindow();
  void RenderTerminalWindow();

  // Actions to take from the hud interface.
  void SaveConfig();

  const GameConfig *config_;
  const Focus *focus_;
  Controller *controller_;
  TextureManager *texture_manager_;

  SDL_Renderer *renderer_;
  ImGuiIO *imgui_io_;
  ImGuiContext *imgui_context_;
  ImVec4 clear_color_;

  // Copy of the config to modify and save.
  GameConfig hud_config_ = *config_;

  // If the log size hasn't changed, we don't need to update the log.
  int log_size_ = 0;

  // If creating scenes, store them here.
  int active_scene_ = -1;
  std::vector<Scene> scenes_;
  absl::flat_hash_set<int> removed_scenes_;
};

}  // namespace zebes