#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/level_panel_interface.h"
#include "editor/level_editor/level_panel_model.h"
#include "editor/level_editor/palette_panel.h"
#include "editor/level_editor/level_selection_state.h"
#include "editor/level_editor/parallax_theme_panel.h"
#include "editor/level_editor/parallax_zone_panel.h"
#include "editor/level_editor/viewport_tab.h"
#include "objects/level.h"

namespace zebes {

// Vertical space reserved for the main editor workspace and bottom palette.
struct LevelEditorPanelLayout {
  float workspace_height = 0.0f;
  float palette_height = 0.0f;
};

// Divides the available editor height while keeping both panels reachable.
LevelEditorPanelLayout CalculateLevelEditorPanelLayout(float available_height);

class LevelEditor {
 public:
  struct Options {
    Api* api = nullptr;
    GuiInterface* gui = nullptr;
    std::unique_ptr<LevelPanelInterface> level_panel;
    std::unique_ptr<ParallaxThemePanel> parallax_theme_panel;
    std::unique_ptr<ParallaxZonePanel> parallax_zone_panel;
    std::unique_ptr<PalettePanel> palette_panel;
  };

  // Creates a new instance of the LevelEditor.
  // Returns an error if the API pointer is null.
  static absl::StatusOr<std::unique_ptr<LevelEditor>> Create(Options options);

  ~LevelEditor() = default;

  // Renders the main Level Editor UI.
  absl::Status Render();

 private:
  friend class LevelEditorTestPeer;

  explicit LevelEditor(Api* api, GuiInterface* gui);

  absl::Status Init(Options options);
  void RefreshLevelCatalog();
  absl::Status SaveActiveLevel();
  absl::Status HandleLevelPanelEvent(LevelPanelEvent event);

  // Renders the level list and management controls.
  absl::Status RenderNavigator();  // Left

  // Renders the main editing viewport.
  absl::Status RenderViewport();  // Middle

  // Renders the properties/details panel for the selected object.
  absl::Status RenderInspector();  // Right

  // Renders the full-width palette panel below the 3-column workspace.
  absl::Status RenderPalette();

  Api* api_;
  GuiInterface* gui_;

  // Sub-Panels
  std::unique_ptr<LevelPanelInterface> level_panel_;
  std::unique_ptr<ParallaxThemePanel> parallax_theme_panel_;
  std::unique_ptr<ParallaxZonePanel> parallax_zone_panel_;
  std::unique_ptr<PalettePanel> palette_panel_;

  // Center viewport
  std::unique_ptr<ViewportTab> viewport_tab_;

  // State
  LevelPanelModel level_model_;
  SelectionState selection_;
  std::optional<std::string> save_error_;
};

}  // namespace zebes
