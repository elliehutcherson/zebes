#pragma once

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "artwork/source_artwork.h"
#include "editor/confirm_prompt.h"
#include "editor/gui_interface.h"
#include "editor/prop_artwork_editor/prop_artwork_editor_model.h"
#include "terrain/terrain_recipe.h"

namespace zebes {

class PropArtworkControlsPanel {
 public:
  enum class Action {
    kNone,
    kBrowseSource,
    kOpenSource,
    kDeleteSource,
  };

  static absl::StatusOr<std::unique_ptr<PropArtworkControlsPanel>> Create(GuiInterface* gui);

  absl::StatusOr<Action> Render(PropArtworkEditorModel& model,
                                const std::vector<SourceArtwork>& sources,
                                const std::vector<TerrainRecipe>& terrain_recipes);

 private:
  explicit PropArtworkControlsPanel(GuiInterface* gui) : gui_(gui) {}

  Action RenderSource(PropArtworkEditorModel& model, const std::vector<SourceArtwork>& sources);
  absl::Status RenderTerrain(PropArtworkEditorModel& model,
                             const std::vector<TerrainRecipe>& terrain_recipes);
  bool RenderPipeline(PropArtworkEditorModel& model);

  GuiInterface* gui_;
  ConfirmPrompt delete_source_prompt_;
};

}  // namespace zebes
