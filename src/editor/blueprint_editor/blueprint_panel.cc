#include "editor/blueprint_editor/blueprint_panel.h"

#include <cfloat>
#include <memory>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/editor_utils.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "objects/blueprint.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<BlueprintPanel>> BlueprintPanel::Create(GuiInterface* gui) {
  if (gui == nullptr) return absl::InvalidArgumentError("GUI can not be null.");
  return absl::WrapUnique(new BlueprintPanel(gui));
}

BlueprintPanel::BlueprintPanel(GuiInterface* gui) : gui_(gui) {}

absl::StatusOr<BlueprintPanel::Event> BlueprintPanel::Render(BlueprintPanelModel& model) {
  if (model.has_active_blueprint()) return RenderDetails(model);
  return RenderList(model);
}

absl::StatusOr<BlueprintPanel::Event> BlueprintPanel::RenderList(BlueprintPanelModel& model) {
  if (gui_->Button("Create")) model.BeginNewBlueprint();
  gui_->SameLine();

  if (gui_->Button("Edit") && model.has_blueprint_selection()) {
    RETURN_IF_ERROR(model.BeginEditingSelectedBlueprint());
  }
  gui_->SameLine();

  {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete") && model.has_blueprint_selection()) {
      return Event{.action = Action::kDelete};
    }
  }

  if (auto list_box = gui_->CreateScopedListBox("Blueprints", ImVec2(-FLT_MIN, -FLT_MIN));
      list_box) {
    for (const auto& catalog_entry : model.blueprints()) {
      const Blueprint& blueprint = catalog_entry.second;
      const bool is_selected = model.selected_blueprint_id() == blueprint.id;
      if (gui_->Selectable(blueprint.name_id().c_str(), is_selected)) {
        RETURN_IF_ERROR(model.SelectBlueprint(blueprint.id));
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  return Event{};
}

absl::StatusOr<BlueprintPanel::Event> BlueprintPanel::RenderDetails(BlueprintPanelModel& model) {
  if (gui_->Button("Back")) {
    model.CloseActiveBlueprint();
    return Event{};
  }

  Blueprint* blueprint = model.active_blueprint();
  if (blueprint == nullptr) return absl::FailedPreconditionError("No blueprint is being edited");

  gui_->Text("ID: %s", blueprint->id.empty() ? "<auto>" : blueprint->id.c_str());
  gui_->InputText("Name", &blueprint->name);

  ASSIGN_OR_RETURN(int state_index, RenderStateList(model));

  gui_->Separator();
  const float button_width = CalculateButtonWidth(gui_, /*num_buttons=*/1);
  if (gui_->Button("Save", ImVec2(button_width, 0))) {
    return Event{.action = Action::kSave};
  }

  if (state_index >= 0) {
    return Event{.action = Action::kEditState, .state_index = state_index};
  }
  return Event{};
}

absl::StatusOr<int> BlueprintPanel::RenderStateList(BlueprintPanelModel& model) {
  gui_->Separator();
  gui_->Text("States");
  gui_->SameLine();
  if (gui_->Button("Add State")) RETURN_IF_ERROR(model.AddState());

  Blueprint* blueprint = model.active_blueprint();
  if (blueprint == nullptr) return absl::FailedPreconditionError("No blueprint is being edited");

  int selected_state_index = -1;
  int state_index = 0;
  while (state_index < static_cast<int>(blueprint->states.size())) {
    ScopedId id = gui_->CreateScopedId(state_index);
    const bool deleted =
        RenderStateDetails(blueprint->states[state_index], state_index, &selected_state_index);
    if (deleted) {
      RETURN_IF_ERROR(model.DeleteState(state_index));
      continue;
    }
    ++state_index;
  }
  return selected_state_index;
}

bool BlueprintPanel::RenderStateDetails(const Blueprint::State& state, int state_index,
                                         int* selected_state_index) {
  gui_->Text("%s", state.name.c_str());
  gui_->SameLine();
  if (gui_->Button("Edit")) *selected_state_index = state_index;
  gui_->SameLine();
  return gui_->Button("X");
}

}  // namespace zebes
