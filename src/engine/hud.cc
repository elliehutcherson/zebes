#include "hud.h"

#include <string>

#include "SDL_video.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "absl/status/status.h"

#include "config.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Hud>> Hud::Create(const GameConfig* config, 
  const Focus* focus, SDL_Window* window, SDL_Renderer* renderer) {
  std::unique_ptr<Hud> hud(new Hud(config, focus));
  absl::Status result = hud->Init(window, renderer);
  if (!result.ok()) return result;
  return hud;
}

Hud::Hud(const GameConfig* config, const Focus* focus) :
config_(config), focus_(focus) {}

absl::Status Hud::Init(SDL_Window* window, SDL_Renderer* renderer) {
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
  
  ImGui::Begin("Hello, world!"); 
  ImGui::Text("%s", focus_->to_string().c_str());
  ImGui::End(); 

  // End of frame: render Dear ImGui
  ImGui::Render();
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
}


}  // namespace zebes 