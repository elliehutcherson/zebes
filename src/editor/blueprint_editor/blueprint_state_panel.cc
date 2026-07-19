#include "editor/blueprint_editor/blueprint_state_panel.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<BlueprintStatePanel>> BlueprintStatePanel::Create(GuiInterface* gui) {
  if (gui == nullptr) {
    return absl::InvalidArgumentError("GUI cannot be null");
  }
  return absl::WrapUnique(new BlueprintStatePanel(gui));
}

BlueprintStatePanel::BlueprintStatePanel(GuiInterface* gui) : gui_(gui) {}

void BlueprintStatePanel::SetState(Blueprint& blueprint, int index) {
  blueprint_ = &blueprint;
  index_ = index;
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

  gui_->InputText("Name", &blueprint_->states[index_].name);

  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(true);
    gui_->InputText("Sprite ID", &blueprint_->states[index_].sprite_id);
    gui_->InputText("Collider ID", &blueprint_->states[index_].collider_id);
  }
}

}  // namespace zebes
