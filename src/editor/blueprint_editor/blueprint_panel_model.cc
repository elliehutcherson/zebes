#include "editor/blueprint_editor/blueprint_panel_model.h"

#include <utility>

#include "absl/status/status.h"

namespace zebes {

void BlueprintPanelModel::SetBlueprints(std::vector<Blueprint> blueprints) {
  blueprints_.clear();
  for (Blueprint& blueprint : blueprints) {
    AssetCatalogKey key{.display_name = blueprint.name, .id = blueprint.id};
    blueprints_.emplace(std::move(key), std::move(blueprint));
  }
  if (FindBlueprint(selected_blueprint_id_) == nullptr) ClearBlueprintSelection();
}

absl::Status BlueprintPanelModel::SelectBlueprint(const std::string& id) {
  if (FindBlueprint(id) == nullptr) return absl::NotFoundError("Blueprint was not found");
  selected_blueprint_id_ = id;
  return absl::OkStatus();
}

void BlueprintPanelModel::ClearBlueprintSelection() { selected_blueprint_id_.clear(); }

void BlueprintPanelModel::BeginNewBlueprint() {
  active_blueprint_ = Blueprint{.name = "New Blueprint"};
}

absl::Status BlueprintPanelModel::BeginEditingSelectedBlueprint() {
  const Blueprint* selected = FindBlueprint(selected_blueprint_id_);
  if (selected == nullptr) {
    return absl::FailedPreconditionError("No blueprint is selected");
  }
  active_blueprint_ = *selected;
  return absl::OkStatus();
}

void BlueprintPanelModel::CloseActiveBlueprint() { active_blueprint_.reset(); }

bool BlueprintPanelModel::is_new_blueprint() const {
  return active_blueprint_.has_value() && active_blueprint_->id.empty();
}

Blueprint* BlueprintPanelModel::active_blueprint() {
  return active_blueprint_.has_value() ? &*active_blueprint_ : nullptr;
}

const Blueprint* BlueprintPanelModel::active_blueprint() const {
  return active_blueprint_.has_value() ? &*active_blueprint_ : nullptr;
}

absl::StatusOr<Blueprint> BlueprintPanelModel::BuildSaveRequest() const {
  if (!active_blueprint_.has_value()) {
    return absl::FailedPreconditionError("No blueprint is being edited");
  }
  return *active_blueprint_;
}

absl::Status BlueprintPanelModel::FinishCreate(const std::string& saved_id) {
  if (!active_blueprint_.has_value()) {
    return absl::FailedPreconditionError("No blueprint is being edited");
  }
  if (!active_blueprint_->id.empty()) {
    return absl::FailedPreconditionError("Active blueprint has already been created");
  }
  if (saved_id.empty()) return absl::InvalidArgumentError("Saved blueprint ID cannot be empty");
  active_blueprint_->id = saved_id;
  selected_blueprint_id_ = saved_id;
  return absl::OkStatus();
}

void BlueprintPanelModel::FinishDelete() {
  CloseActiveBlueprint();
  ClearBlueprintSelection();
}

absl::Status BlueprintPanelModel::AddState() {
  if (!active_blueprint_.has_value()) {
    return absl::FailedPreconditionError("No blueprint is being edited");
  }
  active_blueprint_->states.push_back({.name = "new state"});
  return absl::OkStatus();
}

absl::Status BlueprintPanelModel::DeleteState(int state_index) {
  absl::Status status = ValidateStateIndex(state_index);
  if (!status.ok()) return status;
  active_blueprint_->states.erase(active_blueprint_->states.begin() + state_index);
  return absl::OkStatus();
}

absl::Status BlueprintPanelModel::ValidateStateIndex(int state_index) const {
  if (!active_blueprint_.has_value()) {
    return absl::FailedPreconditionError("No blueprint is being edited");
  }
  if (state_index < 0 || state_index >= static_cast<int>(active_blueprint_->states.size())) {
    return absl::OutOfRangeError("Blueprint state index is out of range");
  }
  return absl::OkStatus();
}

const Blueprint* BlueprintPanelModel::FindBlueprint(const std::string& id) const {
  for (const auto& catalog_entry : blueprints_) {
    if (catalog_entry.second.id == id) return &catalog_entry.second;
  }
  return nullptr;
}

}  // namespace zebes
