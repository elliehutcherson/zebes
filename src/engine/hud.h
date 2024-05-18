#include <string>

#include "imgui.h"

#include "absl/status/status.h"

#include "config.h"
#include "player.h"

namespace zebes {

class Hud {
 public:
  Hud(const GameConfig* config, SDL_Window* window, SDL_Renderer* renderer, const Player* player);
  ~Hud() = default;
  // Initialize IMGUI and other elements.
  absl::Status Init();
  // Render the text.
  void Render();

 private:
  // Get player stats.
  std::string GetPlayerStats();
  // Global config. 
  const GameConfig* config_;
  // SDL Related items.
  SDL_Window* window_;
  SDL_Renderer* renderer_;
  // IMGUI Related items.
  ImGuiIO* imgui_io_;
  ImGuiContext* imgui_context_;
  ImVec4 clear_color_;
  // Items related to getting information to display.
  const Player* player_;
};

}  // namespace zebes 