#include "hud.h"

#include <string>

#include "SDL_video.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "absl/status/status.h"

#include "config.h"

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
    : config_(config), focus_(focus), controller_(controller) {}

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

void Hud::Render() {
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  imgui_context_ = ImGui::GetCurrentContext();

  if (config_->mode == GameConfig::Mode::kCreatorMode) {
    ImGui::Begin("Creator Mode");
    ImGui::Text("%s", focus_->to_string().c_str());

    ImGui::Text("Save Path: ");
    ImGui::SameLine();
    ImGui::InputText("##input1", creator_save_path_, 4096);
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
      controller_->set_save_path(creator_save_path_);
      std::cout << "Saving creator state..." << std::endl;
    }

    ImGui::Text("Import Path: ");
    ImGui::SameLine();
    ImGui::InputText("##input2", creator_import_path_, 4096);
    ImGui::SameLine();
    if (ImGui::Button("Import")) {
      controller_->set_import_path(creator_import_path_);
      std::cout << "Importing layer..." << std::endl;
    }

  } else if (config_->mode == GameConfig::Mode::kPlayerMode) {
    ImGui::Begin("Player Mode");
    ImGui::Text("%s", focus_->to_string().c_str());
  }
  ImGui::End();

  // End of frame: render Dear ImGui
  ImGui::Render();
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
}

} // namespace zebes