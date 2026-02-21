#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/level_panel_interface.h"
#include "editor/level_editor/level_selection_state.h"
#include "editor/level_editor/parallax_preview_tab.h"
#include "editor/level_editor/parallax_theme_panel.h"
#include "editor/level_editor/parallax_zone_panel.h"
#include "editor/level_editor/viewport_tab.h"
#include "objects/level.h"

namespace zebes {

class LevelEditor {
 public:
  struct Options {
    Api* api = nullptr;
    GuiInterface* gui = nullptr;
    std::unique_ptr<LevelPanelInterface> level_panel;
    std::unique_ptr<ParallaxThemePanel> parallax_theme_panel;
    std::unique_ptr<ParallaxZonePanel> parallax_zone_panel;
  };

  // Creates a new instance of the LevelEditor.
  // Returns an error if the API pointer is null.
  static absl::StatusOr<std::unique_ptr<LevelEditor>> Create(Options options);

  ~LevelEditor() = default;

  // Renders the main Level Editor UI.
  absl::Status Render();

 private:
  explicit LevelEditor(Api* api, GuiInterface* gui);

  absl::Status Init(Options options);

  // Renders the level list and management controls.
  absl::Status RenderNavigator();  // Left

  // Renders the main editing viewport.
  absl::Status RenderViewport();  // Middle

  // Renders the properties/details panel for the selected object.
  absl::Status RenderInspector();  // Right

  Api* api_;
  GuiInterface* gui_;

  // Sub-Panels
  std::unique_ptr<LevelPanelInterface> level_panel_;
  std::unique_ptr<ParallaxThemePanel> parallax_theme_panel_;
  std::unique_ptr<ParallaxZonePanel> parallax_zone_panel_;

  // Center Tabs
  std::unique_ptr<ParallaxPreviewTab> parallax_tab_;
  std::unique_ptr<ViewportTab> viewport_tab_;

  // State
  std::optional<Level> editting_level_;
  SelectionState selection_;
  std::optional<std::string> save_error_;
};

}  // namespace zebes
