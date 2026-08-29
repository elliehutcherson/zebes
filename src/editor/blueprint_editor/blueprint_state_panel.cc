#include "editor/blueprint_editor/blueprint_state_panel.h"

#include <array>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"

namespace zebes {
namespace {

constexpr std::array<BlueprintPlacementMode, 3> kPlacementModes = {
    BlueprintPlacementMode::kGrounded,
    BlueprintPlacementMode::kCeiling,
    BlueprintPlacementMode::kFree,
};

const char* PlacementModeLabel(BlueprintPlacementMode mode) {
  switch (mode) {
    case BlueprintPlacementMode::kGrounded:
      return "Grounded";
    case BlueprintPlacementMode::kCeiling:
      return "Ceiling";
    case BlueprintPlacementMode::kFree:
      return "Free";
  }
  return "Invalid";
}

}  // namespace

absl::StatusOr<std::unique_ptr<BlueprintStatePanel>> BlueprintStatePanel::Create(
    GuiInterface* gui) {
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

std::optional<BlueprintPlacementMode> BlueprintStatePanel::GetPlacementMode() const {
  if (blueprint_ == nullptr || index_ < 0 || index_ >= blueprint_->states.size()) {
    return std::nullopt;
  }
  return blueprint_->states[index_].placement_mode;
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

  Blueprint::State& state = blueprint_->states[index_];
  gui_->InputText("Key", &state.key);
  gui_->InputText("Name", &state.name);

  if (ScopedCombo combo =
          gui_->CreateScopedCombo("Placement", PlacementModeLabel(state.placement_mode));
      combo) {
    for (BlueprintPlacementMode candidate : kPlacementModes) {
      const bool selected = candidate == state.placement_mode;
      if (gui_->Selectable(PlacementModeLabel(candidate), selected)) {
        state.placement_mode = candidate;
      }
    }
  }

  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(true);
    gui_->InputText("Sprite ID", &state.sprite_id);
    gui_->InputText("Collider ID", &state.collider_id);
  }
}

}  // namespace zebes
