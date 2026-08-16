#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "editor/confirm_prompt.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/level_selection_state.h"
#include "editor/level_editor/world_layer_model.h"
#include "objects/level.h"

namespace zebes {

class WorldLayerPanel {
 public:
  static absl::StatusOr<std::unique_ptr<WorldLayerPanel>> Create(GuiInterface* gui);

  absl::Status RenderNavigator(Level& level, WorldLayerModel& model, SelectionState& selection);
  absl::Status RenderDetails(Level& level, WorldLayerModel& model, SelectionState& selection);

 private:
  explicit WorldLayerPanel(GuiInterface* gui) : gui_(gui) {}

  GuiInterface* gui_;
  ConfirmPrompt delete_prompt_;
};

}  // namespace zebes
