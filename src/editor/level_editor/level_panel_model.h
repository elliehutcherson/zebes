#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "editor/asset_catalog.h"
#include "objects/level.h"

namespace zebes {

// Owns the level catalog and active editable level without depending on ImGui,
// SDL, or the application API.
class LevelPanelModel {
 public:
  using LevelCatalog = std::map<AssetCatalogKey, Level>;

  void SetLevels(std::vector<Level> levels);
  const LevelCatalog& levels() const { return levels_; }

  absl::Status SelectLevel(const std::string& id);
  void ClearLevelSelection();
  bool has_level_selection() const { return !selected_level_id_.empty(); }
  const std::string& selected_level_id() const { return selected_level_id_; }

  void BeginNewLevel();
  absl::Status BeginEditingSelectedLevel();
  void BeginEditingLevel(Level level);
  void CloseActiveLevel();
  bool has_active_level() const { return active_level_.has_value(); }
  bool is_new_level() const;
  Level* active_level();
  const Level* active_level() const;

  absl::StatusOr<Level> BuildSaveRequest() const;
  absl::Status FinishCreate(const std::string& saved_id);
  void FinishDelete();

 private:
  const Level* FindLevel(const std::string& id) const;

  LevelCatalog levels_;
  std::string selected_level_id_;
  std::optional<Level> active_level_;
};

}  // namespace zebes
