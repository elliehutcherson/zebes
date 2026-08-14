#pragma once

#include <cstdint>
#include <memory>

#include "absl/status/statusor.h"
#include "editor/blueprint_editor/blueprint_panel_model.h"
#include "editor/confirm_prompt.h"
#include "editor/gui_interface.h"

namespace zebes {

// Renders blueprint authoring state and reports persistence intents to the
// containing editor. It does not access the application API.
class BlueprintPanel {
 public:
  enum class Action : std::uint8_t {
    kNone,
    kSave,
    kDelete,
    kEditState,
  };

  struct Event {
    Action action = Action::kNone;
    int state_index = -1;
  };

  static absl::StatusOr<std::unique_ptr<BlueprintPanel>> Create(GuiInterface* gui);

  absl::StatusOr<Event> Render(BlueprintPanelModel& model);

 private:
  explicit BlueprintPanel(GuiInterface* gui);

  absl::StatusOr<Event> RenderList(BlueprintPanelModel& model);
  absl::StatusOr<Event> RenderDetails(BlueprintPanelModel& model);
  absl::StatusOr<int> RenderStateList(BlueprintPanelModel& model);
  bool RenderStateDetails(const Blueprint::State& state, int state_index,
                          int* selected_state_index);

  // Deleting a blueprint destroys every state authored on it, and closing one
  // with edits throws those away. Both ask first, against a remembered target.
  ConfirmPrompt delete_blueprint_prompt_;
  ConfirmPrompt discard_edits_prompt_;

  GuiInterface* gui_;
};

}  // namespace zebes
