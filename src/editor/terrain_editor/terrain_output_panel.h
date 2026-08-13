#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "editor/gui_interface.h"
#include "editor/terrain_editor/terrain_editor_model.h"
#include "objects/texture.h"

namespace zebes {

// What the terrain tab is about to produce, and the button that produces it.
//
// Kept apart from the tuning controls because these are decisions about the
// asset -- its name, where its pixels come from, how finely they are rendered
// -- rather than about how the material looks.
class TerrainOutputPanel {
 public:
  enum class Action {
    kNone,
    // The caller should run the creation routine for the model's source. It
    // blocks for seconds, so the panel reports rather than performs it.
    kCreate,
  };

  static absl::StatusOr<std::unique_ptr<TerrainOutputPanel>> Create(GuiInterface* gui);

  // textures populates the picker used when importing a manifest, which
  // describes artwork that already exists.
  absl::StatusOr<Action> Render(TerrainEditorModel& model, const std::vector<Texture>& textures);

 private:
  explicit TerrainOutputPanel(GuiInterface* gui) : gui_(gui) {}

  bool RenderSourceSelector(TerrainEditorModel& model);
  void RenderTexturePicker(TerrainEditorModel& model, const std::vector<Texture>& textures);
  void RenderSummary(TerrainEditorModel& model);

  GuiInterface* gui_;
};

}  // namespace zebes
