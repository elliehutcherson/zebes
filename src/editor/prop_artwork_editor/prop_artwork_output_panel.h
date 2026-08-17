#pragma once

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "artwork/prop_recipe.h"
#include "editor/confirm_prompt.h"
#include "editor/gui_interface.h"
#include "editor/prop_artwork_editor/prop_artwork_editor_model.h"

namespace zebes {

class PropArtworkOutputPanel {
 public:
  enum class Action {
    kNone,
    kOpenRecipe,
    kClearWorkspace,
    kCopyRecipe,
    kPrepare,
    kCommit,
    kDelete,
  };

  static absl::StatusOr<std::unique_ptr<PropArtworkOutputPanel>> Create(GuiInterface* gui);

  Action Render(PropArtworkEditorModel& model, const std::vector<PropRecipe>& recipes,
                bool work_in_progress);

 private:
  explicit PropArtworkOutputPanel(GuiInterface* gui) : gui_(gui) {}

  Action RenderRecipeSelector(PropArtworkEditorModel& model,
                              const std::vector<PropRecipe>& recipes);
  bool RenderDeleteControl(const PropArtworkEditorModel& model);
  void RenderPreviewPolicy(PropArtworkEditorModel& model);

  GuiInterface* gui_;
  ConfirmPrompt clear_prompt_;
  ConfirmPrompt delete_prompt_;
};

}  // namespace zebes
