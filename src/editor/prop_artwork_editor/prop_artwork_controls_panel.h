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
#include "editor/prop_artwork_editor/prop_artwork_editor_model.h"
#include "terrain/terrain_recipe.h"

namespace zebes {

struct PropGenerationProviderStatus {
  std::string name;
  bool available = false;
  std::string unavailable_reason;
};

// What the panel needs to know about generation without reaching an engine.
// The editor owns provider selection and the composition root owns engines.
struct PropGenerationStatus {
  std::vector<PropGenerationProviderStatus> providers;
  size_t selected_provider = 0;
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
    kSelectGenerationProvider,
    kGenerate,
    kCancelGeneration,
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
  Action RenderCandidates(PropArtworkEditorModel& model);
  absl::Status RenderTerrain(PropArtworkEditorModel& model,
                             const std::vector<TerrainRecipe>& terrain_recipes);
  bool RenderPipeline(PropArtworkEditorModel& model);

  GuiInterface* gui_;
  ConfirmPrompt delete_source_prompt_;
};

}  // namespace zebes
