#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "editor/inspector_ui.h"
#include "editor/level_editor/level_selection_state.h"
#include "editor/level_editor/parallax_zone_creation_model.h"
#include "objects/level.h"

namespace zebes {

class ParallaxZonePanel {
 public:
  enum class ThemeAction { kEdit, kDuplicateAndAssign };
  struct ThemeRequest {
    ThemeAction action;
    int zone_id;
    std::string theme_id;
  };

  struct Options {
    Api* api = nullptr;
    GuiInterface* gui = nullptr;
  };

  static absl::StatusOr<std::unique_ptr<ParallaxZonePanel>> Create(Options options);

  // Renders the list of Zones in the Navigator.
  absl::Status RenderNavigator(Level& level, SelectionState& selection);

  // Renders details for a selected Zone in the Inspector.
  absl::Status RenderDetails(Level& level, SelectionState& selection);
  // Renders the transient creation draft and returns the committed stable ID.
  std::optional<int> RenderCreation(Level& level, SelectionState& selection);
  std::optional<ThemeRequest> TakeThemeRequest();

 private:
  friend class ParallaxZonePanelTestPeer;

  explicit ParallaxZonePanel(Options options);
  absl::Status RenderThemePicker(InspectorPropertyGrid& grid, std::string& theme_id,
                                 const std::vector<ParallaxTheme>& themes);

  Api* api_;
  GuiInterface* gui_;
  std::optional<ThemeRequest> theme_request_;
  ParallaxZoneCreationModel creation_model_;
  std::string theme_search_;
  std::optional<std::string> creation_error_;
};

}  // namespace zebes
