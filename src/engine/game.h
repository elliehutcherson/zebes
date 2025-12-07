#pragma once

#include <memory>

#include "absl/status/status.h"
#include "api/api.h"
#include "common/config.h"
#include "db/db_interface.h"
#include "engine/camera_interface.h"
#include "engine/collision_manager.h"
#include "engine/controller.h"
#include "engine/focus.h"
#include "engine/focus_lite.h"
#include "engine/sprite_manager.h"
#include "imgui.h"

namespace zebes {

class Game {
 public:
  Game(GameConfig config);
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

  GameConfig config_;
  uint64_t frame_start_ = 0;
  uint64_t frame_time_ = 0;
  bool is_running_ = false;
  bool enable_frame_by_frame_ = false;
  bool advance_frame_by_frame_ = false;

  SDL_Window* window_;
  SDL_Renderer* renderer_;
  SDL_GLContext gl_context_;
  ImGuiContext* imgui_context_;
  ImGuiIO imgui_io_;

  std::unique_ptr<FocusLite> focus_lite_;
  std::unique_ptr<CameraInterface> camera_;
  std::unique_ptr<DbInterface> db_;
  std::unique_ptr<SpriteManager> sprite_manager_;
  std::unique_ptr<CollisionManager> collision_manager_;
  std::unique_ptr<Api> api_;
  std::unique_ptr<Controller> controller_;

  // Focus pointer to update the camera.
  Focus* focus_;
};

}  // namespace zebes