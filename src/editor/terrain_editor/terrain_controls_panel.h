#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "editor/gui_interface.h"
#include "editor/terrain_editor/terrain_editor_model.h"

namespace zebes {

// The tuning controls for a generated terrain.
//
// Sections run in the order a person decides things -- what material this is,
// then how its surface reads, then its interior, then how large a patch of it
// repeats -- rather than in the order the generator consumes them. Every
// control carries a tooltip, because the labels are the generator's own
// vocabulary and mean nothing on sight.
class TerrainControlsPanel {
 public:
  static absl::StatusOr<std::unique_ptr<TerrainControlsPanel>> Create(GuiInterface* gui);

  // Renders the controls for the model's current source. Returns true when a
  // control moved, which is the caller's cue that the preview is stale.
  bool Render(TerrainEditorModel& model);

 private:
  explicit TerrainControlsPanel(GuiInterface* gui) : gui_(gui) {}

  // Renders one labelled control with its explanation attached.
  void Explain(const char* description);

  bool RenderThemeSection(TerrainEditorModel& model);
  bool RenderSurfaceSection(TerrainGenConfig& config);
  bool RenderInteriorSection(TerrainGenConfig& config);
  bool RenderPatternSection(TerrainEditorModel& model);
  bool RenderManifestSection(TerrainEditorModel& model);

  GuiInterface* gui_;
};

}  // namespace zebes
