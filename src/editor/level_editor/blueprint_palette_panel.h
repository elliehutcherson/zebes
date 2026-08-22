#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/blueprint_palette_model.h"
#include "objects/blueprint.h"

namespace zebes {

// Renders a searchable thumbnail grid of blueprints below the main viewport.
// The selected blueprint is surfaced to the LevelEditor so that the viewport
// can enter placement mode.
class BlueprintPalettePanel {
 public:
  struct Options {
    Api* api = nullptr;
    GuiInterface* gui = nullptr;
  };

  static absl::StatusOr<std::unique_ptr<BlueprintPalettePanel>> Create(Options options);

  // Renders the palette browser. Must be called each frame.
  absl::Status Render();

  // Returns the currently selected blueprint, or nullptr if none is selected.
  const Blueprint* GetSelectedBlueprint() const;

  // Deselects the current blueprint (e.g. after a placement or Escape).
  void ClearSelection() { model_.ClearSelection(); }

  // Returns whether snap-to-grid is toggled on in the UI.
  bool GetSnapToGrid() const { return snap_to_grid_; }

  // Returns whether entity borders should be drawn in the viewport.
  bool GetShowEntityBorders() const { return show_entity_borders_; }

  // Returns the yellow overlay opacity [0,1] applied to entities in the
  // viewport and to blueprint buttons in the palette. 0 = off, 1 = fully yellow.
  float GetEntityOverlayOpacity() const { return entity_overlay_opacity_; }

 private:
  friend class BlueprintPalettePanelTestPeer;

  explicit BlueprintPalettePanel(Options options);

  Api& api_;
  GuiInterface* gui_;

  BlueprintPaletteModel model_;
  bool snap_to_grid_ = true;
  bool show_entity_borders_ = false;
  float entity_overlay_opacity_ = 0.0f;
};

}  // namespace zebes
