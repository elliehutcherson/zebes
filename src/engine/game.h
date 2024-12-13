#pragma once

#include "imgui.h"

#include "absl/status/status.h"

#include "nlohmann/json.hpp"

#include "camera.h"
#include "collision_manager.h"
#include "config.h"
#include "controller.h"
#include "creator.h"
#include "focus.h"
#include "hud.h"
#include "map.h"
#include "object.h"
#include "player.h"
#include "sprite_manager.h"
#include "tile_manager.h"

namespace zebes {

class Game {
public:
  Game(const GameConfig &config);
  ~Game() = default;

  // Initialize all SDL objects and state.
  absl::Status Init();
  // Game loop.
  absl::Status Run();
  // Anything that needs to run before the pipeline.
  void PrePipeline();
  // Allow interfaces to inject events before calling handle events.
  void InjectEvents();
  // Handle inputs from user or other events.
  void HandleEvents();
  // Update any state that needs updating before rendering.
  void Update();
  // Draw the game.
  void Render();
  // Clear any state that needs clearing after render.
  void Clear();
  // Quit the game.
  void Clean();
  // Check if the game is running.
  bool IsRunning();
  // Check if it's okay to advance to the next frame.
  bool AdvanceFrame();

private:
  // Update game state local to this object.
  void GameUpdate();
  // Delay the game loop for the next frame until we meet the delay
  // specified in the config.
  void GameDelay();

  const GameConfig config_;
  uint64_t frame_start_ = 0;
  uint64_t frame_time_ = 0;
  bool is_running_ = false;
  bool enable_frame_by_frame_ = false;
  bool advance_frame_by_frame_ = false;

  SDL_Window *window_;
  SDL_Renderer *renderer_;
  SDL_GLContext gl_context_;
  ImGuiContext *imgui_context_;
  ImGuiIO imgui_io_;

  std::unique_ptr<Camera> camera_;
  std::unique_ptr<SpriteManager> sprite_manager_;
  std::unique_ptr<CollisionManager> collision_manager_;
  std::unique_ptr<TextureManager> texture_manager_;
  std::unique_ptr<TileManager> tile_manager_;
  std::unique_ptr<Player> player_;
  std::unique_ptr<Creator> creator_;
  std::unique_ptr<Object> object_;
  std::unique_ptr<Map> map_;
  std::unique_ptr<Hud> hud_;
  std::unique_ptr<Controller> controller_;

  // Focus pointer to update the camera.
  Focus *focus_;
};

} // namespace zebes