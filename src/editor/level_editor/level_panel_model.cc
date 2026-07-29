#include "editor/level_editor/level_panel_model.h"

#include <utility>

#include "absl/status/status.h"
#include "editor/level_editor/level_tiles.h"

namespace zebes {

void LevelPanelModel::SetLevels(std::vector<Level> levels) {
  levels_.clear();
  for (Level& level : levels) {
    AssetCatalogKey key{.display_name = level.name, .id = level.id};
    levels_.emplace(std::move(key), std::move(level));
  }
  if (FindLevel(selected_level_id_) == nullptr) ClearLevelSelection();
}

void LevelPanelModel::SetTilesetChoices(std::vector<TilesetChoice> choices) {
  tileset_choices_ = std::move(choices);
}

std::string LevelPanelModel::ActiveTilesetName() const {
  if (active_level_ == std::nullopt || active_level_->tileset_id.empty()) return "";
  for (const TilesetChoice& choice : tileset_choices_) {
    if (choice.id == active_level_->tileset_id) return choice.name;
  }
  return active_level_->tileset_id;
}

absl::Status LevelPanelModel::RequestTilesetChange(const std::string& tileset_id) {
  if (!active_level_.has_value()) {
    return absl::FailedPreconditionError("No level is being edited");
  }
  if (tileset_id.empty()) return absl::InvalidArgumentError("Tileset ID cannot be empty");

  pending_tileset_id_.reset();
  if (tileset_id == active_level_->tileset_id) return absl::OkStatus();

  // Nothing is placed, so no tile ID can be reinterpreted by the new tileset.
  if (!LevelHasTiles(*active_level_)) {
    active_level_->tileset_id = tileset_id;
    return absl::OkStatus();
  }

  pending_tileset_id_ = tileset_id;
  return absl::OkStatus();
}

std::string LevelPanelModel::pending_tileset_id() const {
  return pending_tileset_id_.value_or("");
}

int LevelPanelModel::placed_tile_count() const {
  if (!active_level_.has_value()) return 0;
  return CountPlacedTiles(*active_level_);
}

absl::Status LevelPanelModel::ConfirmTilesetChange() {
  if (!active_level_.has_value()) {
    return absl::FailedPreconditionError("No level is being edited");
  }
  if (!pending_tileset_id_.has_value()) {
    return absl::FailedPreconditionError("No tileset change is pending");
  }

  // The tiles cannot come along: their IDs name artwork in the old tileset.
  active_level_->tile_chunks.clear();
  active_level_->tileset_id = *std::move(pending_tileset_id_);
  pending_tileset_id_.reset();
  return absl::OkStatus();
}

void LevelPanelModel::CancelTilesetChange() { pending_tileset_id_.reset(); }

absl::Status LevelPanelModel::SelectLevel(const std::string& id) {
  if (FindLevel(id) == nullptr) return absl::NotFoundError("Level was not found");
  selected_level_id_ = id;
  return absl::OkStatus();
}

void LevelPanelModel::ClearLevelSelection() { selected_level_id_.clear(); }

void LevelPanelModel::BeginNewLevel() { BeginEditingLevel(Level{.name = "name"}); }

absl::Status LevelPanelModel::BeginEditingSelectedLevel() {
  const Level* selected = FindLevel(selected_level_id_);
  if (selected == nullptr) return absl::FailedPreconditionError("No level is selected");
  BeginEditingLevel(*selected);
  return absl::OkStatus();
}

void LevelPanelModel::BeginEditingLevel(Level level) {
  active_level_ = std::move(level);
  // A staged tileset change belongs to the level it was requested for.
  pending_tileset_id_.reset();
}

void LevelPanelModel::CloseActiveLevel() {
  active_level_.reset();
  pending_tileset_id_.reset();
}

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
