#include "editor/level_editor/level_panel_model.h"

#include <utility>

#include "absl/status/status.h"

namespace zebes {

void LevelPanelModel::SetLevels(std::vector<Level> levels) {
  levels_.clear();
  for (Level& level : levels) {
    AssetCatalogKey key{.display_name = level.name, .id = level.id};
    levels_.emplace(std::move(key), std::move(level));
  }
  if (FindLevel(selected_level_id_) == nullptr) ClearLevelSelection();
}

absl::Status LevelPanelModel::SelectLevel(const std::string& id) {
  if (FindLevel(id) == nullptr) return absl::NotFoundError("Level was not found");
  selected_level_id_ = id;
  return absl::OkStatus();
}

void LevelPanelModel::ClearLevelSelection() { selected_level_id_.clear(); }

void LevelPanelModel::BeginNewLevel() { active_level_ = Level{.name = "name"}; }

absl::Status LevelPanelModel::BeginEditingSelectedLevel() {
  const Level* selected = FindLevel(selected_level_id_);
  if (selected == nullptr) return absl::FailedPreconditionError("No level is selected");
  BeginEditingLevel(*selected);
  return absl::OkStatus();
}

void LevelPanelModel::BeginEditingLevel(Level level) { active_level_ = std::move(level); }

void LevelPanelModel::CloseActiveLevel() { active_level_.reset(); }

bool LevelPanelModel::is_new_level() const {
  return active_level_.has_value() && active_level_->id.empty();
}

Level* LevelPanelModel::active_level() {
  return active_level_.has_value() ? &*active_level_ : nullptr;
}

const Level* LevelPanelModel::active_level() const {
  return active_level_.has_value() ? &*active_level_ : nullptr;
}

absl::StatusOr<Level> LevelPanelModel::BuildSaveRequest() const {
  if (!active_level_.has_value()) {
    return absl::FailedPreconditionError("No level is being edited");
  }
  return *active_level_;
}

absl::Status LevelPanelModel::FinishCreate(const std::string& saved_id) {
  if (!active_level_.has_value()) {
    return absl::FailedPreconditionError("No level is being edited");
  }
  if (!active_level_->id.empty()) {
    return absl::FailedPreconditionError("Active level has already been created");
  }
  if (saved_id.empty()) return absl::InvalidArgumentError("Saved level ID cannot be empty");
  active_level_->id = saved_id;
  selected_level_id_ = saved_id;
  return absl::OkStatus();
}

void LevelPanelModel::FinishDelete() {
  CloseActiveLevel();
  ClearLevelSelection();
}

const Level* LevelPanelModel::FindLevel(const std::string& id) const {
  for (const auto& catalog_entry : levels_) {
    if (catalog_entry.second.id == id) return &catalog_entry.second;
  }
  return nullptr;
}

}  // namespace zebes
