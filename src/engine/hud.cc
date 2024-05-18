#include "hud.h"

#include <string>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "config.h"
#include "object.h"
#include "polygon.h"

namespace zebes {

Hud::Hud(const GameConfig* config, SDL_Window* window, SDL_Renderer* renderer, const Player* player) :
config_(config), window_(window), renderer_(renderer), player_(player) {}


absl::Status Hud::Init() {
   // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  imgui_io_ = &ImGui::GetIO();
  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_);
  ImGui_ImplSDLRenderer2_Init(renderer_);
  clear_color_ = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
  return absl::OkStatus();
}

std::string Hud::GetPlayerStats() {
  const MobileObject* player_object = player_->GetObject();
  const Polygon* polygon = player_object->polygon();
  std::string message = absl::StrFormat("[x1, x2]: %d, %d\n", polygon->x_min_floor(), polygon->x_max_floor());
  absl::StrAppend(&message, absl::StrFormat("[y1, y2]: %d, %d\n", polygon->y_min_floor(), polygon->y_max_floor()));
  absl::StrAppend(&message, absl::StrFormat("stype: %d\n", player_object->GetActiveSpriteProfile()->type)); 
  absl::StrAppend(&message, absl::StrFormat("sticks: %d\n", player_object->GetActiveSpriteTicks())); 
  absl::StrAppend(&message, absl::StrFormat("scycle: %d\n", player_object->GetActiveSpriteCycles())); 
  absl::StrAppend(&message, absl::StrFormat("is_grounded: %d\n", player_->is_grounded())); 
  absl::StrAppend(&message, absl::StrFormat("velocity_x: %f\n", player_->velocity_x())); 
  absl::StrAppend(&message, absl::StrFormat("velocity_y: %f\n", player_->velocity_x()));
  return message;
}

void Hud::Render() {
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  
  ImGui::Begin("Hello, world!"); 
  ImGui::Text(GetPlayerStats().c_str());
  ImGui::End(); 

  // End of frame: render Dear ImGui
  ImGui::Render();
  SDL_SetRenderDrawColor(renderer_, (Uint8)(clear_color_.x * 255), (Uint8)(clear_color_.y * 255), (Uint8)(clear_color_.z * 255), (Uint8)(clear_color_.w * 255));
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
}


}  // namespace zebes 