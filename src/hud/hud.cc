#include "hud/hud.h"

#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "common/config.h"
#include "common/status_macros.h"
#include "engine/controller.h"
#include "hud/boundary_config.h"
#include "hud/collision_config.h"
#include "hud/sprite_creator.h"
#include "hud/texture_creator.h"
#include "hud/window_config.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_internal.h"

namespace zebes {
namespace {

constexpr ImGuiWindowFlags kWindowFlags =
    ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

void BuildDocking(const ImGuiID& dockspace_id) {
  ImGui::DockBuilderRemoveNode(dockspace_id);  // Clear out existing layout
  ImGui::DockBuilderAddNode(dockspace_id,
                            ImGuiDockNodeFlags_None);  // Add empty node

  ImGuiID dock_main_id = dockspace_id;
  ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(
      dock_main_id, ImGuiDir_Right, 0.2f, nullptr, &dock_main_id);
  ImGuiID dock_left_id = ImGui::DockBuilderSplitNode(
      dock_main_id, ImGuiDir_Left, 0.2f, nullptr, &dock_main_id);
  ImGuiID dock_up_id = ImGui::DockBuilderSplitNode(
      dock_main_id, ImGuiDir_Up, 0.05f, nullptr, &dock_main_id);
  ImGuiID dock_down_id = ImGui::DockBuilderSplitNode(
      dock_main_id, ImGuiDir_Down, 0.2f, nullptr, &dock_main_id);

  ImGui::DockBuilderDockWindow("Scene", dock_main_id);
  ImGui::DockBuilderDockWindow("Config", dock_left_id);
  ImGui::DockBuilderDockWindow("Assets", dock_right_id);
  ImGui::DockBuilderDockWindow("Editor", dock_up_id);
  ImGui::DockBuilderDockWindow("Console", dock_down_id);

  ImGui::DockBuilderFinish(dockspace_id);
}

}  // namespace

absl::StatusOr<std::unique_ptr<Hud>> Hud::Create(Hud::Options options) {
  if (options.config == nullptr)
    return absl::InvalidArgumentError("Config must not be null.");
  if (options.focus == nullptr)
    return absl::InvalidArgumentError("Focus must not be null.");
  if (options.controller == nullptr)
    return absl::InvalidArgumentError("Controller must not be null.");
  if (options.sprite_manager == nullptr)
    return absl::InvalidArgumentError("SpriteManager must not be null.");
  if (options.api == nullptr)
    return absl::InvalidArgumentError("Api must not be null.");
  if (options.renderer == nullptr)
    return absl::InvalidArgumentError("Renderer must not be null.");
  if (options.window == nullptr)
    return absl::InvalidArgumentError("Window must not be null.");

  std::unique_ptr<Hud> hud(new Hud(options));
  RETURN_IF_ERROR(hud->Init());
  return hud;
}

Hud::Hud(Options options)
    : original_config_(options.config),
      focus_(options.focus),
      controller_(options.controller),
      sprite_manager_(options.sprite_manager),
      api_(options.api),
      renderer_(options.renderer),
      window_(options.window) {}

absl::Status Hud::Init() {
  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  imgui_io_ = &ImGui::GetIO();
  imgui_io_->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_);
  ImGui_ImplSDLRenderer2_Init(renderer_);

  auto save_config_callback = [&]() { SaveConfig(); };

  HudBoundaryConfig::Options boundary_options = {
      .hud_config = &hud_config_.boundaries,
      .original_config = &hud_config_.boundaries,
      .save_config_callback = save_config_callback};
  ASSIGN_OR_RETURN(HudBoundaryConfig hud_boundary_config,
                   HudBoundaryConfig::Create(std::move(boundary_options)));
  hud_boundary_config_ =
      std::make_unique<HudBoundaryConfig>(std::move(hud_boundary_config));

  HudCollisionConfig::Options collision_options = {
      .hud_config = &hud_config_.collisions,
      .original_config = &hud_config_.collisions,
      .save_config_callback = save_config_callback};
  ASSIGN_OR_RETURN(HudCollisionConfig hud_collision_config,
                   HudCollisionConfig::Create(std::move(collision_options)));
  hud_collision_config_ =
      std::make_unique<HudCollisionConfig>(std::move(hud_collision_config));

  HudTileConfig::Options tile_options = {
      .hud_config = &hud_config_.tiles,
      .original_config = &hud_config_.tiles,
      .save_config_callback = save_config_callback};
  ASSIGN_OR_RETURN(HudTileConfig hud_tile_config,
                   HudTileConfig::Create(std::move(tile_options)));
  hud_tile_config_ =
      std::make_unique<HudTileConfig>(std::move(hud_tile_config));

  HudWindowConfig::Options window_options = {
      .hud_config = &hud_config_.window,
      .original_config = &hud_config_.window,
      .save_config_callback = save_config_callback};
  ASSIGN_OR_RETURN(HudWindowConfig hud_window_config,
                   HudWindowConfig::Create(std::move(window_options)));
  hud_window_config_ =
      std::make_unique<HudWindowConfig>(std::move(hud_window_config));

  LOG(INFO) << "Zebes: Initializing texture creator...";
  HudTextureCreator::Options texture_options = {
      .sprite_manager = sprite_manager_, .api = api_};
  ASSIGN_OR_RETURN(HudTextureCreator hud_texture_creator,
                   HudTextureCreator::Create(texture_options));
  hud_texture_creator_ =
      std::make_unique<HudTextureCreator>(std::move(hud_texture_creator));

  LOG(INFO) << "Zebes: Initializing sprite creator...";
  HudSpriteCreator::Options sprite_options = {
      .sprite_manager = sprite_manager_,
      .texture_creator = hud_texture_creator_.get()};
  ASSIGN_OR_RETURN(HudSpriteCreator hud_sprite_creator,
                   HudSpriteCreator::Create(std::move(sprite_options)));
  hud_sprite_creator_ =
      std::make_unique<HudSpriteCreator>(std::move(hud_sprite_creator));

  LOG(INFO) << "Zebes: Initializing terminal...";
  hud_terminal_ = std::make_unique<HudTerminal>();

  LOG(INFO) << "Zebes: Hud initialization complete...";
  return absl::OkStatus();
}

void Hud::InjectEvents() {
  bool is_main_window_focused = !ImGui::GetIO().WantCaptureMouse;
  if (is_main_window_focused) return;
  controller_->AddInternalEvent(
      {.type = InternalEventType::kIsMainWindowFocused,
       .value = is_main_window_focused});
}

void Hud::Update() {
  hud_sprite_creator_->Update();
  hud_texture_creator_->Update();
}

void Hud::Render() {
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  imgui_context_ = ImGui::GetCurrentContext();

  if (original_config_->mode == GameConfig::Mode::kCreatorMode) {
    RenderCreatorMode();
  } else if (original_config_->mode == GameConfig::Mode::kPlayerMode) {
    RenderPlayerMode();
  }

  // End of frame: render Dear ImGui
  ImGui::Render();
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
}

void Hud::RenderPlayerMode() {
  ImGui::Begin("Player Mode");
  ImGui::Text("%s", focus_->to_string().c_str());
  ImGui::End();
}

void Hud::RenderCreatorMode() {
  bool open = true;

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);
  ImGui::SetNextWindowViewport(viewport->ID);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

  ImGui::Begin("DockSpace Demo", &open, kWindowFlags);
  ImGui::PopStyleVar(3);

  ImGuiID dockspace_id = ImGui::GetID("dockspace_id");
  if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr || rebuild_docks_) {
    BuildDocking(dockspace_id);
    rebuild_docks_ = false;
  }

  ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), 0);
  ImGui::End();

  // Left side of the dock
  ImGui::Begin("Config", &open, 0);
  if (ImGui::CollapsingHeader("Camera Info")) {
    ImGui::Text("%s", focus_->to_string().c_str());
  }
  if (ImGui::CollapsingHeader("Window Config")) {
    hud_window_config_->Render();
  }
  if (ImGui::CollapsingHeader("Boundary Config")) {
    hud_boundary_config_->Render();
  }
  if (ImGui::CollapsingHeader("Tile Config")) {
    hud_tile_config_->Render();
  }
  if (ImGui::CollapsingHeader("Collision Config")) {
    hud_collision_config_->Render();
  }
  ImGui::End();

  // Right side of the dock
  ImGui::Begin("Assets", &open, 0);
  if (ImGui::CollapsingHeader("Textures")) {
    hud_texture_creator_->Render();
  }
  if (ImGui::CollapsingHeader("Sprites")) {
    hud_sprite_creator_->RenderSpriteList();
  }
  ImGui::End();

  // Top side of the dock
  ImGui::Begin("Editor", &open, 0);
  hud_sprite_creator_->RenderEditor();
  ImGui::End();

  // Bottom side of the dock
  ImGui::Begin("Console", &open, 0);
  if (ImGui::CollapsingHeader("Terminal")) {
    hud_terminal_->Render();
  }
  ImGui::End();
}

void Hud::HandleEvent(const SDL_Event& event) {
  ImGui_ImplSDL2_ProcessEvent(&event);
}

void Hud::SaveConfig() {
  LOG(INFO) << "Saving creator state..." << std::endl;
  controller_->AddInternalEvent(
      {.type = InternalEventType::kCreatorSaveConfig, .value = hud_config_});
}

}  // namespace zebes
