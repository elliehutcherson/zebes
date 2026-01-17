#include "editor/blueprint_editor/blueprint_state_panel.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<BlueprintStatePanel>> BlueprintStatePanel::Create(
    Api* api, GuiInterface* gui) {
  if (api == nullptr) {
    return absl::InvalidArgumentError("API cannot be null");
  }
  if (gui == nullptr) {
    return absl::InvalidArgumentError("GUI cannot be null");
  }
  return absl::WrapUnique(new BlueprintStatePanel(api, gui));
}

BlueprintStatePanel::BlueprintStatePanel(Api* api, GuiInterface* gui) : api_(api), gui_(gui) {}

void BlueprintStatePanel::SetState(Blueprint& blueprint, int index) {
  blueprint_ = &blueprint;
  index_ = index;

  if (index_ >= 0 && index_ < blueprint_->states.size()) {
    current_name_ = blueprint_->states[index].name;
  } else {
    current_name_.clear();
  }
}

void BlueprintStatePanel::Reset() {
  blueprint_ = nullptr;
  index_ = -1;
}

void BlueprintStatePanel::Render() {
  if (blueprint_ == nullptr || index_ < 0 || index_ >= blueprint_->states.size()) {
    return;
  }

  gui_->Separator();
  gui_->Text("Blueprint State");

  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(true);
    gui_->InputInt("Index", &index_);
  }

  if (gui_->InputText("Name", &blueprint_->states[index_].name)) {
    current_name_ = blueprint_->states[index_].name;
  }

  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(true);
    gui_->InputText("Sprite ID", &blueprint_->states[index_].sprite_id);
    gui_->InputText("Collider ID", &blueprint_->states[index_].collider_id);
  }
}

}  // namespace zebes