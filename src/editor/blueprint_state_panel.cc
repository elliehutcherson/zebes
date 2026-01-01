#include "editor/blueprint_state_panel.h"

#include <iterator>

#include "absl/log/log.h"
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

void BlueprintStatePanel::SetState(Blueprint* blueprint, int index) {
  blueprint_ = blueprint;
  index_ = index;

  if (blueprint_ && index_ >= 0 && index_ < blueprint_->states.size()) {
    current_name_ = *std::next(blueprint_->states.begin(), index_);
  } else {
    current_name_.clear();
  }
}

void BlueprintStatePanel::Reset() { SetState(nullptr, -1); }

void BlueprintStatePanel::Render() {
  if (blueprint_ == nullptr || index_ < 0 || index_ >= blueprint_->states.size()) {
    return;
  }

  ImGui::Separator();
  ImGui::Text("Blueprint State");

  ImGui::BeginDisabled();
  ImGui::InputInt("Index", &index_);
  ImGui::EndDisabled();

  ImGui::InputText("Name", &current_name_);

  ImGui::BeginDisabled();
  std::string sprite_id;
  if (blueprint_->sprite_ids.find(index_) != blueprint_->sprite_ids.end()) {
    sprite_id = blueprint_->sprite_ids[index_];
  }
  ImGui::InputText("Sprite ID", &sprite_id);

  std::string collider_id;
  if (blueprint_->collider_ids.find(index_) != blueprint_->collider_ids.end()) {
    collider_id = blueprint_->collider_ids[index_];
  }
  ImGui::InputText("Collider ID", &collider_id);
  ImGui::EndDisabled();

  if (ImGui::Button("Save")) {
    ConfirmState();
  }
}

void BlueprintStatePanel::ConfirmState() {
  if (blueprint_ == nullptr || index_ < 0 || index_ >= blueprint_->states.size()) {
    return;
  }

  // Get current state name to remove
  auto it = std::next(blueprint_->states.begin(), index_);
  std::string old_name = *it;

  if (old_name == current_name_) {
    return;  // No change
  }

  blueprint_->states.erase(it);
  blueprint_->states.insert(current_name_);

  absl::Status status = api_->UpdateBlueprint(*blueprint_);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to save blueprint state: " << status.message();
  } else {
    LOG(INFO) << "Updated blueprint state: " << old_name << " -> " << current_name_;
  }
}

}  // namespace zebes
