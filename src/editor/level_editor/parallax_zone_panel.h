#pragma once

#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/level_selection_state.h"
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
  std::optional<ThemeRequest> TakeThemeRequest();

 private:
  friend class ParallaxZonePanelTestPeer;

  explicit ParallaxZonePanel(Options options);

  Api* api_;
  GuiInterface* gui_;
  std::optional<ThemeRequest> theme_request_;
};

}  // namespace zebes
