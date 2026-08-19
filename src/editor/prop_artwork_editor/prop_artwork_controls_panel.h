#pragma once

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "artwork/source_artwork.h"
#include "editor/confirm_prompt.h"
#include "editor/gui_interface.h"
#include "editor/image_generation/image_generation.h"
#include "editor/prop_artwork_editor/prop_artwork_editor_model.h"
#include "terrain/terrain_recipe.h"

namespace zebes {

// What the panel needs to know about the provider without reaching it. The
// editor owns the engine; the panel only bounds its controls by what the
// adapter says it can do and by whether a request is already running.
struct PropGenerationStatus {
  ImageGenerationCapabilities capabilities;
  bool in_flight = false;
};

class PropArtworkControlsPanel {
 public:
  enum class Action {
    kNone,
    kBrowseSource,
    kOpenSource,
    kDeleteSource,
    kGenerate,
    kCancelGeneration,
    kAcceptCandidate,
    kDiscardCandidates,
  };

  static absl::StatusOr<std::unique_ptr<PropArtworkControlsPanel>> Create(GuiInterface* gui);

  absl::StatusOr<Action> Render(PropArtworkEditorModel& model,
                                const std::vector<SourceArtwork>& sources,
                                const std::vector<TerrainRecipe>& terrain_recipes,
                                const PropGenerationStatus& generation);

 private:
  explicit PropArtworkControlsPanel(GuiInterface* gui) : gui_(gui) {}

  Action RenderSource(PropArtworkEditorModel& model, const std::vector<SourceArtwork>& sources);
  Action RenderGeneration(PropArtworkEditorModel& model, const PropGenerationStatus& generation);
  Action RenderCandidates(PropArtworkEditorModel& model);
  absl::Status RenderTerrain(PropArtworkEditorModel& model,
                             const std::vector<TerrainRecipe>& terrain_recipes);
  bool RenderPipeline(PropArtworkEditorModel& model);

  GuiInterface* gui_;
  ConfirmPrompt delete_source_prompt_;
};

}  // namespace zebes
