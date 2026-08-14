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

// A tileset the active level may be bound to. Only identity is needed here;
// the panel never reads tile data.
struct TilesetChoice {
  std::string id;
  std::string name;
};

// Owns the level catalog and active editable level without depending on ImGui,
// SDL, or the application API.
class LevelPanelModel {
 public:
  using LevelCatalog = std::map<AssetCatalogKey, Level>;

  void SetLevels(std::vector<Level> levels);
  const LevelCatalog& levels() const { return levels_; }

  // Tilesets offered by the active level's Tileset field, refreshed by the
  // editor from the API.
  void SetTilesetChoices(std::vector<TilesetChoice> choices);
  const std::vector<TilesetChoice>& tileset_choices() const { return tileset_choices_; }

  // Name of the active level's tileset, or its raw ID when the catalog does
  // not have it, or empty when the level is unbound.
  std::string ActiveTilesetName() const;

  // Points the active level at a tileset. A level with no tiles rebinds
  // immediately; otherwise the request is staged, because the level's placed
  // IDs name different artwork under a different tileset and switching has to
  // discard them. Selecting the level's current tileset is always a no-op.
  absl::Status RequestTilesetChange(const std::string& tileset_id);

  // Whether a change is waiting on confirmation, and what it would cost.
  bool has_pending_tileset_change() const { return pending_tileset_id_.has_value(); }
  std::string pending_tileset_id() const;
  int placed_tile_count() const;

  // Applies the staged change, erasing every placed tile. Fails when nothing
  // is staged rather than silently doing nothing.
  absl::Status ConfirmTilesetChange();
  void CancelTilesetChange();

  absl::Status SelectLevel(const std::string& id);
  void ClearLevelSelection();
  bool has_level_selection() const { return !selected_level_id_.empty(); }
  const std::string& selected_level_id() const { return selected_level_id_; }

  void BeginNewLevel();
  absl::Status BeginEditingSelectedLevel();
  void BeginEditingLevel(Level level);
  void CloseActiveLevel();
  bool has_active_level() const { return active_level_.has_value(); }

  // Whether the level being edited differs from the state it was opened or last
  // saved at. Only asked on the frame the user tries to leave, so comparing the
  // whole level -- tile chunks included -- costs nothing per frame and cannot
  // miss an edit the way a flag some mutating path forgot to set would.
  bool has_unsaved_changes() const;
  bool is_new_level() const;
  Level* active_level();
  const Level* active_level() const;

  absl::StatusOr<Level> BuildSaveRequest() const;
  absl::Status FinishCreate(const std::string& saved_id);
  // Makes the current state the clean one. Called after a successful write, so
  // further edits compare against what is on disk rather than against how the
  // level looked when it was opened.
  void MarkSaved();
  void FinishDelete();

 private:
  const Level* FindLevel(const std::string& id) const;

  LevelCatalog levels_;
  std::string selected_level_id_;
  std::optional<Level> active_level_;
  // The active level as it stood when editing began or when it was last saved.
  std::optional<Level> baseline_level_;
  std::vector<TilesetChoice> tileset_choices_;
  // Set only while a tileset change is waiting on confirmation.
  std::optional<std::string> pending_tileset_id_;
};

}  // namespace zebes
