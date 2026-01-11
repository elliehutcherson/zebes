#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/level_editor/level_panel_interface.h"
#include "editor/level_editor/parallax_panel.h"

namespace zebes {

class LevelEditor {
 public:
  struct Options {
    Api* api = nullptr;
    std::unique_ptr<LevelPanelInterface> level_panel;
    std::unique_ptr<ParallaxPanel> parallax_panel;
  };

  // Creates a new instance of the LevelEditor.
  // Returns an error if the API pointer is null.
  static absl::StatusOr<std::unique_ptr<LevelEditor>> Create(Options options);

  ~LevelEditor() = default;

  // Renders the main Level Editor UI.
  // This uses a 3-column layout:
  // - Left: Level List (Management)
  // - Center: Viewport (Visual Editor)
  // - Right: Details (Inspector)
  absl::Status Render();

 private:
  explicit LevelEditor(Api* api);

  absl::Status Init(Options options);

  // Renders the level list and management controls.
  absl::Status RenderLeft();

  // Renders the main editing viewport.
  void RenderCenter();

  // Renders the properties/details panel for the selected object.
  void RenderRight();

  Api* api_;
  std::unique_ptr<LevelPanelInterface> level_panel_;
  std::unique_ptr<ParallaxPanel> parallax_panel_;

  // State
  std::optional<Level> editting_level_;
};

}  // namespace zebes
