#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/level_selection_state.h"
#include "objects/level.h"

namespace zebes {

class ParallaxZonePanel {
 public:
  struct Options {
    GuiInterface* gui = nullptr;
  };

  static absl::StatusOr<std::unique_ptr<ParallaxZonePanel>> Create(Options options);

  // Renders the list of Zones in the Navigator.
  absl::Status RenderNavigator(Level& level, SelectionState& selection);

  // Renders details for a selected Zone in the Inspector.
  absl::Status RenderDetails(Level& level, SelectionState& selection);

 private:
  friend class ParallaxZonePanelTestPeer;

  explicit ParallaxZonePanel(Options options);

  GuiInterface* gui_;
};

}  // namespace zebes
