#pragma once

#include <memory>

#include "SDL_render.h"
#include "SDL_video.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "common/config.h"
#include "engine/controller.h"
#include "engine/focus.h"
#include "engine/sprite_manager.h"
#include "hud/boundary_config.h"
#include "hud/collision_config.h"
#include "hud/sprite_creator.h"
#include "hud/terminal.h"
#include "hud/texture_creator.h"
#include "hud/tile_config.h"
#include "hud/window_config.h"

namespace zebes {

class Hud {
 public:
  struct Options {
    const GameConfig *config;
    const Focus *focus;
    Controller *controller;
    SpriteManager *sprite_manager;
    Api *api;

    SDL_Window *window;
    SDL_Renderer *renderer;
  };

  static absl::StatusOr<std::unique_ptr<Hud>> Create(Options options);

  ~Hud() = default;

  // Inject events to the controller, like if ImGui is capturing the mouse.
  void InjectEvents();

  void Update();

  void Render();

  void HandleEvent(const SDL_Event &event);

 private:
  Hud(Options options);

  absl::Status Init();

  // Update the position selection on the texture currently rendered in the
  // editor.
  void UpdateSpriteEditorSelection();

  void RenderDemoMode();
  void RenderCreatorMode();
  void RenderPlayerMode();

  // Actions to take from the hud interface._;
  void SaveConfig();

  // Zebes library state.
  const GameConfig *original_config_;
  const Focus *focus_;
  Controller *controller_;
  SpriteManager *sprite_manager_;
  Api *api_;

  // SDL and IMGUI state.
  SDL_Renderer *renderer_;
  SDL_Window *window_;
  ImGuiIO *imgui_io_;
  ImGuiContext *imgui_context_;
  const ImVec4 clear_color_ = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  // Hud windows.
  std::unique_ptr<HudBoundaryConfig> hud_boundary_config_;
  std::unique_ptr<HudCollisionConfig> hud_collision_config_;
  std::unique_ptr<HudTileConfig> hud_tile_config_;
  std::unique_ptr<HudWindowConfig> hud_window_config_;
  std::unique_ptr<HudTextureCreator> hud_texture_creator_;
  std::unique_ptr<HudSpriteCreator> hud_sprite_creator_;
  std::unique_ptr<HudTerminal> hud_terminal_;

  // Hud specific state.
  // The config that the hud is editing.
  GameConfig hud_config_ = *original_config_;
  // Whether or not rebuilding the docks is necessary.
  bool rebuild_docks_ = false;
};

}  // namespace zebes