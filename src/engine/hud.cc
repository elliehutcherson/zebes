#include "hud.h"

#include <string>
#include <vector>

#include "SDL_video.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "engine/config.h"
#include "engine/controller.h"
#include "engine/logging.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

namespace zebes {
namespace {

bool IsMouseOverRect(const ImVec2 &position, const ImVec2 &size) {
  ImGuiIO &io = ImGui::GetIO();
  ImVec2 mouse_position = io.MousePos;
  return mouse_position.x >= position.x &&
         mouse_position.x <= position.x + size.x &&
         mouse_position.y >= position.y &&
         mouse_position.y <= position.y + size.y;
}

void RenderTextureToImGui(SDL_Texture *texture, int width, int height) {
  // Create a temporary ImGui texture ID
  ImTextureID texture_id = (ImTextureID)(intptr_t)texture;
  ImVec2 size = ImVec2(width, height);

  // Position must be set before rendering the image.
  ImVec2 position = ImGui::GetCursorScreenPos();
  ImGui::Image(texture_id, size);

  // Check if the texture was clicked.
  if (IsMouseOverRect(position, size) && ImGui::IsMouseClicked(0)) {
    LOG(INFO) << "Texture clicked!";
  }
}

}  // namespace

absl::StatusOr<std::unique_ptr<Hud>> Hud::Create(Hud::Options options) {
  std::unique_ptr<Hud> hud(new Hud(options));
  if (options.config == nullptr)
    return absl::InvalidArgumentError("Config must not be null.");
  if (options.focus == nullptr)
    return absl::InvalidArgumentError("Focus must not be null.");
  if (options.controller == nullptr)
    return absl::InvalidArgumentError("Controller must not be null.");
  if (options.texture_manager == nullptr)
    return absl::InvalidArgumentError("TextureManager must not be null.");
  absl::Status result = hud->Init(options.window, options.renderer);
  if (!result.ok()) return result;
  return hud;
}

Hud::Hud(Options options)
    : config_(options.config),
      focus_(options.focus),
      controller_(options.controller),
      texture_manager_(options.texture_manager) {}

absl::Status Hud::Init(SDL_Window *window, SDL_Renderer *renderer) {
  renderer_ = renderer;
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
  if (is_main_window_focused) return;
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
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
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
    RenderSceneWindowPrimary();
  }
  if (ImGui::CollapsingHeader("Textures")) {
    RenderTextureWindow();
  }
  if (ImGui::CollapsingHeader("Terminal")) {
    RenderTerminalWindow();
  }

  ImGui::End();
}

void Hud::RenderWindowConfig() {
  ImGui::InputInt("window_width", &hud_config_.window.width);
  ImGui::InputInt("window_height", &hud_config_.window.height);
  if (ImGui::Button("Apply")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting window config...";
    hud_config_.window = config_->window;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default window config...";
    hud_config_.window = WindowConfig();
  }
}

void Hud::RenderBoundaryConfig() {
  ImGui::InputInt("boundary_x_min", &hud_config_.boundaries.x_min);
  ImGui::InputInt("boundary_x_max", &hud_config_.boundaries.x_max);
  ImGui::InputInt("boundary_y_min", &hud_config_.boundaries.y_min);
  ImGui::InputInt("boundary_y_max", &hud_config_.boundaries.y_max);
  if (ImGui::Button("Apply")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting boundary config...";
    hud_config_.boundaries = config_->boundaries;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default boundary config...";
    hud_config_.boundaries = BoundaryConfig();
  }
}

void Hud::RenderTileConfig() {
  ImGui::InputInt("tile_scale", &hud_config_.tiles.scale);
  ImGui::InputInt("tile_source_width", &hud_config_.tiles.source_width);
  ImGui::InputInt("tile_source_height", &hud_config_.tiles.source_height);
  ImGui::InputInt("tile_size_x", &hud_config_.tiles.size_x);
  ImGui::InputInt("tile_size_y", &hud_config_.tiles.size_y);
  ImGui::Text("tile_render_width: %d",
              hud_config_.tiles.source_width * hud_config_.tiles.scale);
  ImGui::Text("tile_render_height: %d",
              hud_config_.tiles.source_height * hud_config_.tiles.scale);
  if (ImGui::Button("Apply")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting tile config...";
    hud_config_.tiles = config_->tiles;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default tile config...";
    hud_config_.tiles = TileConfig();
  }
}

void Hud::RenderCollisionConfig() {
  ImGui::InputFloat("area_width", &hud_config_.collisions.area_width);
  ImGui::InputFloat("area_height", &hud_config_.collisions.area_height);
  if (ImGui::Button("Apply")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting tile config...";
    hud_config_.collisions = config_->collisions;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default tile config...";
    hud_config_.collisions = CollisionConfig();
  }
}

void Hud::RenderSceneWindowPrimary() {
  if (ImGui::Button("Add Scene")) {
    int index = scenes_.size();
    scenes_.push_back({.index = index});
  }

  for (int i = 0; i < scenes_.size(); i++) {
    if (ImGui::CollapsingHeader(absl::StrFormat("Scene %d", i).c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      RenderSceneWindow(i);
    }
  }
}

void Hud::RenderSceneWindow(int index) {
  Scene &scene = scenes_[index];
  scene.index = index;
  ImGui::PushID(index);
  // ImGui::Text("Scene %d", scene.index);
  ImGui::PopID();

  std::string label = absl::StrCat("##SceneSave", scene.index);
  ImGui::Text("Save Path: ");
  ImGui::SameLine();
  ImGui::InputText(label.c_str(), scene.save_path, 4096);
  ImGui::SameLine();
  if (ImGui::Button("Save")) {
    SaveConfig();
  }

  label = absl::StrCat("##SceneImport", scene.index);
  ImGui::Text("Import Path: ");
  ImGui::SameLine();
  ImGui::InputText(label.c_str(), scene.import_path, 4096);
  ImGui::SameLine();
  if (ImGui::Button("Import")) {
    LOG(INFO) << "Importing layer..." << std::endl;
  }

  if (ImGui::Button("Remove Scene")) {
    if (scene.index == active_scene_) {
      active_scene_ = -1;
    }
    removed_scenes_.insert(scene.index);
  }

  ImGui::SameLine();

  label = "Activate";
  bool pop_color = false;
  if (scene.index == active_scene_) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(0.4f, 0.8f, 0.4f, 1.0f));  // Greenish button
    label = "Deactivate";
    pop_color = true;
  }
  if (ImGui::Button(label.c_str())) {
    active_scene_ = scene.index;
  }
  if (pop_color) {
    ImGui::PopStyleColor();
  }
}

void Hud::RenderTextureWindow() {
  RenderTextureToImGui(texture_manager_->Experiment(), 256, 256);
}

void Hud::RenderTerminalWindow() {
  ImGui::BeginChild("Log");
  ImGui::TextUnformatted(HudLogSink::Get()->log()->c_str());
  if (log_size_ != HudLogSink::Get()->log()->size()) {
    ImGui::SetScrollHereY(1.0f);
    log_size_ = HudLogSink::Get()->log()->size();
  }
  ImGui::EndChild();
}

void Hud::SaveConfig() {
  LOG(INFO) << "Saving creator state..." << std::endl;
  controller_->AddInternalEvent(
      {.type = InternalEventType::kCreatorSaveConfig, .value = hud_config_});
}

}  // namespace zebes
