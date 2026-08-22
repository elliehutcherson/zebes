#include "editor/level_editor/tileset_selector.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/api.h"
#include "common/status_macros.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "objects/tileset.h"

namespace zebes {

absl::StatusOr<TilesetSelectorResult> TilesetSelector::Render(Api& api, GuiInterface& gui,
                                                              const char* combo_label,
                                                              std::string_view item_id_prefix) {
  if (combo_label == nullptr || item_id_prefix.empty()) {
    return absl::InvalidArgumentError("Tileset selector labels must not be empty.");
  }

  const std::vector<Tileset> tilesets = api.GetAllTilesets();
  auto selected = tilesets.end();
  if (selected_id_.has_value()) {
    selected = std::find_if(tilesets.begin(), tilesets.end(),
                            [this](const Tileset& tileset) { return tileset.id == *selected_id_; });
  }

  bool selection_changed = false;
  if (selected_id_.has_value() && selected == tilesets.end()) {
    selected_id_.reset();
    selection_changed = true;
  }

  const char* preview = selected == tilesets.end() ? "(none)" : selected->name.c_str();
  if (ScopedCombo combo = gui.CreateScopedCombo(combo_label, preview); combo) {
    for (const Tileset& tileset : tilesets) {
      const bool is_selected = selected_id_.has_value() && *selected_id_ == tileset.id;
      const std::string label =
          absl::StrCat(tileset.name.empty() ? "(unnamed tileset)" : tileset.name, "##",
                       item_id_prefix, tileset.id);
      if (!gui.Selectable(label.c_str(), is_selected)) continue;

      selection_changed = !is_selected;
      Select(tileset.id);
    }
  }

  if (!selected_id_.has_value()) {
    return TilesetSelectorResult{
        .selection_changed = selection_changed,
        .catalog_empty = tilesets.empty(),
    };
  }

  ASSIGN_OR_RETURN(Tileset * resolved, api.GetTileset(*selected_id_));
  return TilesetSelectorResult{
      .tileset = resolved,
      .selection_changed = selection_changed,
      .catalog_empty = tilesets.empty(),
  };
}

void TilesetSelector::Select(std::string tileset_id) {
  if (tileset_id.empty()) {
    selected_id_.reset();
    return;
  }
  selected_id_ = std::move(tileset_id);
}

}  // namespace zebes
