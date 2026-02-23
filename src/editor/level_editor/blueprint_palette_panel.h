#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "objects/blueprint.h"

namespace zebes {

// Renders a horizontal strip of blueprint buttons below the main viewport.
// The selected blueprint is surfaced to the LevelEditor so that the viewport
// can enter placement mode.
class BlueprintPalettePanel {
 public:
  struct Options {
    Api* api = nullptr;
    GuiInterface* gui = nullptr;
  };

  static absl::StatusOr<std::unique_ptr<BlueprintPalettePanel>> Create(Options options);

  // Renders the palette strip. Must be called each frame.
  absl::Status Render();

  // Returns the currently selected blueprint, or nullptr if none is selected.
  const Blueprint* GetSelectedBlueprint() const { return selected_blueprint_; }

  // Deselects the current blueprint (e.g. after a placement or Escape).
  void ClearSelection() { selected_blueprint_ = nullptr; }

  // Returns whether snap-to-grid is toggled on in the UI.
  bool GetSnapToGrid() const { return snap_to_grid_; }

  // Returns whether entity borders should be drawn in the viewport.
  bool GetShowEntityBorders() const { return show_entity_borders_; }

 private:
  friend class BlueprintPalettePanelTestPeer;

  explicit BlueprintPalettePanel(Options options);

  Api& api_;
  GuiInterface* gui_;

  // Pointer into the Api's blueprint storage; valid as long as the Api is alive.
  const Blueprint* selected_blueprint_ = nullptr;
  bool snap_to_grid_ = true;
  bool show_entity_borders_ = false;
};

}  // namespace zebes
