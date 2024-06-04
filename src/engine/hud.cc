#include "hud.h"

#include <string>
#include <vector>

#include "SDL_video.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "absl/log/log.h"
#include "absl/status/status.h"

#include "engine/controller.h"
#include "engine/config.h"
#include "engine/logging.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Hud>> Hud::Create(Hud::Options options) {
  std::unique_ptr<Hud> hud(
      new Hud(options.config, options.focus, options.controller));
  if (options.config == nullptr)
    return absl::InvalidArgumentError("Config must not be null.");
  if (options.focus == nullptr)
    return absl::InvalidArgumentError("Focus must not be null.");
  if (options.controller == nullptr)
    return absl::InvalidArgumentError("Controller must not be null.");
  absl::Status result = hud->Init(options.window, options.renderer);
  if (!result.ok())
    return result;
  return hud;
}

Hud::Hud(const GameConfig *config, const Focus *focus, Controller *controller)
    : config_(config), focus_(focus), controller_(controller),
      hud_window_config_(config_->window),
      hud_boundary_config_(config_->boundaries),
      hud_tile_config_(config_->tiles) {}

absl::Status Hud::Init(SDL_Window *window, SDL_Renderer *renderer) {
  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  imgui_io_ = &ImGui::GetIO();
  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer2_Init(renderer);
  clear_color_ = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
  return absl::OkStatus();
}

void Hud::InjectEvents() {
  bool is_main_window_focused = !ImGui::GetIO().WantCaptureMouse;
  if (is_main_window_focused)
    return;
  controller_->AddInternalEvent(
      {.type = InternalEventType::kIsMainWindowFocused,
       .value = is_main_window_focused});
}

void Hud::Update() {
  int index = 0;
  std::erase_if(scenes_, [&](const Scene &scene) {
    return removed_scenes_.contains(index++);
  });
  removed_scenes_.clear();
}

void Hud::Render() {
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  imgui_context_ = ImGui::GetCurrentContext();

  if (config_->mode == GameConfig::Mode::kCreatorMode) {
    RenderCreatorMode();
  } else if (config_->mode == GameConfig::Mode::kPlayerMode) {
    RenderPlayerMode();
  }

  // End of frame: render Dear ImGui
  ImGui::Render();
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
}

void Hud::RenderPlayerMode() {
  ImGui::Begin("Player Mode");

  ImGui::Text("%s", focus_->to_string().c_str());

  ImGui::End();
}

void Hud::RenderCreatorMode() {
  ImGui::Begin("Creator Mode");

  if (ImGui::CollapsingHeader("Camera Info")) {
    ImGui::Text("%s", focus_->to_string().c_str());
  }

  if (ImGui::CollapsingHeader("Window Config")) {
    RenderWindowConfig();
  }

  if (ImGui::CollapsingHeader("Boundary Config")) {
    RenderBoundaryConfig();
  }

  if (ImGui::CollapsingHeader("Tile Config")) {
    RenderTileConfig();
  }

  if (ImGui::CollapsingHeader("Collision Config")) {
    RenderCollisionConfig();
  } 

  if (ImGui::CollapsingHeader("Scene Creation")) {
    if (ImGui::Button("Add Scene")) {
      int index = scenes_.size();
      scenes_.push_back({.index = index, .is_active = false});
    }

    for (int i = 0; i < scenes_.size(); i++) {
      if (ImGui::CollapsingHeader(absl::StrFormat("Scene %d", i).c_str(),
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        RenderSceneWindow(i);
      }
    }
  }

  if (ImGui::CollapsingHeader("Terminal")) {
    ImGui::BeginChild("Log");
    ImGui::TextUnformatted(HudLogSink::Get()->log()->c_str());
    if (log_size_ != HudLogSink::Get()->log()->size()) {
      ImGui::SetScrollHereY(1.0f);
      log_size_ = HudLogSink::Get()->log()->size();
    }
    ImGui::EndChild();
  }

  ImGui::End();
}

void Hud::RenderWindowConfig() {
  ImGui::InputInt("window_width", &hud_window_config_.width);
  ImGui::InputInt("window_height", &hud_window_config_.height);
  if (ImGui::Button("Apply")) {
    LOG(INFO) << "Applying window config...";
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting window config...";
    hud_window_config_ = config_->window;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default window config...";
    hud_window_config_ = {};
  }
}

void Hud::RenderBoundaryConfig() {
  ImGui::InputInt("boundary_x_min", &hud_boundary_config_.x_min);
  ImGui::InputInt("boundary_x_max", &hud_boundary_config_.x_max);
  ImGui::InputInt("boundary_y_min", &hud_boundary_config_.y_min);
  ImGui::InputInt("boundary_y_max", &hud_boundary_config_.y_max);
  if (ImGui::Button("Apply")) {
    LOG(INFO) << "Applying boundary config...";
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting boundary config...";
    hud_boundary_config_ = config_->boundaries;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default boundary config...";
    hud_boundary_config_ = {};
  }
}

void Hud::RenderTileConfig() {
  ImGui::InputInt("tile_scale", &hud_tile_config_.scale);
  ImGui::InputInt("tile_source_width", &hud_tile_config_.source_width);
  ImGui::InputInt("tile_source_height", &hud_tile_config_.source_height);
  ImGui::InputInt("tile_size_x", &hud_tile_config_.size_x);
  ImGui::InputInt("tile_size_y", &hud_tile_config_.size_y);
  ImGui::Text("tile_render_width: %d",
              hud_tile_config_.source_width * hud_tile_config_.scale);
  ImGui::Text("tile_render_height: %d",
              hud_tile_config_.source_height * hud_tile_config_.scale);
  if (ImGui::Button("Apply")) {
    LOG(INFO) << "Applying tile config...";
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting tile config...";
    hud_tile_config_.scale = config_->tiles.scale;
    hud_tile_config_.source_width = config_->tiles.source_width;
    hud_tile_config_.source_height = config_->tiles.source_height;
    hud_tile_config_.size_x = config_->tiles.size_x;
    hud_tile_config_.size_y = config_->tiles.size_y;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default tile config...";
    hud_tile_config_ = {};
  }
}

void Hud::RenderCollisionConfig() {
  ImGui::InputFloat("area_width", &hud_collision_config_.area_width);
  ImGui::InputFloat("area_height", &hud_collision_config_.area_height);
  if (ImGui::Button("Apply")) {
    LOG(INFO) << "Applying tile config...";
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting tile config...";
    hud_collision_config_.area_width = config_->collisions.area_width;
    hud_collision_config_.area_height = config_->collisions.area_height;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default tile config...";
    hud_collision_config_ = {};
  }
}

void Hud::RenderSceneWindow(int index) {
  Scene &scene = scenes_[index];
  scene.index = index;
  ImGui::PushID(index);
  ImGui::Checkbox("Active", &scene.is_active);
  ImGui::SameLine();
  ImGui::Text("Scene %d", scene.index);
  ImGui::PopID();

  std::string label = "##SceneSave" + std::to_string(scene.index);
  ImGui::Text("Save Path: ");
  ImGui::SameLine();
  ImGui::InputText(label.c_str(), creator_save_path_, 4096);
  ImGui::SameLine();
  if (ImGui::Button("Save")) {
    controller_->AddInternalEvent({.type = InternalEventType::kCreatorSavePath,
                                   .value = creator_save_path_});
    LOG(INFO) << "Saving creator state..." << std::endl;
  }

  label = "##SceneImport" + std::to_string(scene.index);
  ImGui::Text("Import Path: ");
  ImGui::SameLine();
  ImGui::InputText(label.c_str(), creator_import_path_, 4096);
  ImGui::SameLine();
  if (ImGui::Button("Import")) {
    controller_->AddInternalEvent(
        {.type = InternalEventType::kCreatorImportPath,
         .value = creator_import_path_});
    LOG(INFO) << "Importing layer..." << std::endl;
  }

  if (ImGui::Button("Remove Scene")) {
    removed_scenes_.insert(scene.index);
  }
}

} // namespace zebes
