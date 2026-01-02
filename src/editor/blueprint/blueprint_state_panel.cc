#include "editor/blueprint/blueprint_state_panel.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<BlueprintStatePanel>> BlueprintStatePanel::Create(Api* api) {
  if (api == nullptr) {
    return absl::InvalidArgumentError("API cannot be null");
  }
  return absl::WrapUnique(new BlueprintStatePanel(api));
}

BlueprintStatePanel::BlueprintStatePanel(Api* api) : api_(api) {}

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

  ImGui::Separator();
  ImGui::Text("Blueprint State");

  ImGui::BeginDisabled();
  ImGui::InputInt("Index", &index_);
  ImGui::EndDisabled();

  if (ImGui::InputText("Name", &blueprint_->states[index_].name)) {
    current_name_ = blueprint_->states[index_].name;
  }

  ImGui::BeginDisabled();
  ImGui::InputText("Sprite ID", &blueprint_->states[index_].sprite_id);
  ImGui::InputText("Collider ID", &blueprint_->states[index_].collider_id);
  ImGui::EndDisabled();
}

}  // namespace zebes
