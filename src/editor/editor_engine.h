#pragma once

#include <memory>

#include "SDL.h"
#include "absl/status/status.h"
#include "api/api.h"
#include "common/config.h"
#include "db/db.h"
#include "editor/editor_ui.h"

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
  explicit EditorEngine(GameConfig config);

  // Process SDL events
  void HandleEvents(bool* done);

  // Render one frame
  void RenderFrame();

  // SDL state
  SDL_Window* window_;
  SDL_Renderer* renderer_;

  // Zebes state
  GameConfig config_;
  std::unique_ptr<Db> db_;
  std::unique_ptr<Api> api_;

  // Editor UI
  std::unique_ptr<EditorUi> ui_;
};

}  // namespace zebes
