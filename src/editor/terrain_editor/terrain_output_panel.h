#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "editor/confirm_prompt.h"
#include "editor/gui_interface.h"
#include "editor/terrain_editor/terrain_editor_model.h"
#include "objects/texture.h"
#include "terrain/terrain_recipe.h"

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
    // The caller should start the creation routine for the model's source.
    kCreate,
    kOpenRecipe,
    kNewRecipe,
    kCopyRecipe,
    kRegenerate,
    // The caller should remove the open recipe together with the tileset and
    // the artwork it produced. Only reported after the user confirms.
    kDeleteTerrain,
  };

  static absl::StatusOr<std::unique_ptr<TerrainOutputPanel>> Create(GuiInterface* gui);

  // textures populates the picker used when importing a manifest, which
  // describes artwork that already exists.
  absl::StatusOr<Action> Render(TerrainEditorModel& model, const std::vector<Texture>& textures,
                                const std::vector<TerrainRecipe>& recipes = {},
                                bool work_in_progress = false);

 private:
  explicit TerrainOutputPanel(GuiInterface* gui) : gui_(gui) {}

  bool RenderSourceSelector(TerrainEditorModel& model);
  void RenderTexturePicker(TerrainEditorModel& model, const std::vector<Texture>& textures);
  void RenderSummary(TerrainEditorModel& model);
  Action RenderRecipeSelector(TerrainEditorModel& model, const std::vector<TerrainRecipe>& recipes);
  // True on the frame the user confirms. Only called with a recipe open.
  bool RenderDeleteControl(TerrainEditorModel& model);

  GuiInterface* gui_;

  // Armed against the recipe's ID, so choosing a different recipe while the
  // question is up disarms it rather than repointing it at the new one.
  ConfirmPrompt delete_terrain_prompt_;
};

}  // namespace zebes
