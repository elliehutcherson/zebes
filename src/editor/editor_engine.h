#pragma once

#include <memory>

#include "SDL.h"
#include "absl/status/status.h"
#include "api/api.h"
#include "common/config.h"
#include "common/imgui_wrapper.h"
#include "common/sdl_wrapper.h"
#include "editor/editor_ui.h"
#include "editor/gui.h"
#include "engine/input_manager.h"
#include "resources/blueprint_manager.h"
#include "resources/collider_manager.h"
#include "resources/level_manager.h"
#include "resources/sprite_manager.h"
#include "resources/texture_manager.h"

namespace zebes {

class EditorEngine {
 public:
  static absl::StatusOr<std::unique_ptr<EditorEngine>> Create();

  ~EditorEngine() = default;
  // DELETE Copy Constructor (keep this deleted to prevent expensive copies)
  EditorEngine(const EditorEngine&) = delete;
  EditorEngine& operator=(const EditorEngine&) = delete;

  // ADD Move Constructor
  EditorEngine(EditorEngine&& other) = default;
  EditorEngine& operator=(EditorEngine&& other) = default;

  // Initialize SDL, ImGui, and Zebes components
  absl::Status Init();

  // Run the main editor loop
  absl::Status Run();

  // Shutdown and cleanup
  void Shutdown();

 private:
  explicit EditorEngine(EngineConfig config);

  // Process SDL events
  void HandleEvents(bool* done);

  // Render one frame
  void RenderFrame();

  // SDL state
  std::unique_ptr<SdlWrapper> sdl_;

  // Zebes state
  EngineConfig config_;
  std::unique_ptr<TextureManager> texture_manager_;
  std::unique_ptr<SpriteManager> sprite_manager_;
  std::unique_ptr<ColliderManager> collider_manager_;
  std::unique_ptr<BlueprintManager> blueprint_manager_;
  std::unique_ptr<LevelManager> level_manager_;
  std::unique_ptr<ImGuiWrapper> imgui_wrapper_;
  std::unique_ptr<InputManager> input_manager_;
  std::unique_ptr<Api> api_;

  // Editor UI
  std::unique_ptr<Gui> gui_;
  std::unique_ptr<EditorUi> ui_;
};

}  // namespace zebes
