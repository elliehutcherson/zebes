#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "artwork/source_artwork.h"
#include "editor/confirm_prompt.h"
#include "editor/gui_interface.h"
#include "editor/image_generation/image_generation.h"
#include "editor/image_generation/image_generation_lifecycle_panel.h"
#include "editor/image_generation/image_generation_request_controller.h"
#include "editor/prop_artwork_editor/prop_artwork_editor_model.h"
#include "terrain/terrain_recipe.h"

namespace zebes {

using PropGenerationProviderStatus = ImageGenerationProviderStatus;
using PropGenerationStatus = ImageGenerationUiState;

class PropArtworkControlsPanel {
 public:
  enum class Action {
    kNone,
    kBrowseSource,
    kOpenSource,
    kDeleteSource,
    kSelectGenerationProvider,
    kGenerate,
    kCancelGeneration,
    kSelectCandidate,
    kAcceptCandidate,
    kDiscardCandidates,
  };

  static absl::StatusOr<std::unique_ptr<PropArtworkControlsPanel>> Create(GuiInterface* gui);

  absl::StatusOr<Action> Render(PropArtworkEditorModel& model,
                                const std::vector<SourceArtwork>& sources,
                                const std::vector<TerrainRecipe>& terrain_recipes,
                                PropGenerationStatus& generation);

 private:
  explicit PropArtworkControlsPanel(GuiInterface* gui) : gui_(gui) {}

  Action RenderSource(PropArtworkEditorModel& model, const std::vector<SourceArtwork>& sources);
  Action RenderGeneration(PropArtworkEditorModel& model, PropGenerationStatus& generation);
  absl::Status RenderTerrain(PropArtworkEditorModel& model,
                             const std::vector<TerrainRecipe>& terrain_recipes);
  bool RenderPipeline(PropArtworkEditorModel& model);

  GuiInterface* gui_;
  ConfirmPrompt delete_source_prompt_;
};

}  // namespace zebes
