#include "editor/tileset_editor/tileset_panel.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"
#include "objects/texture.h"
#include "objects/tileset.h"

namespace zebes {
namespace {

// Width of the terrain name field, leaving room for the mask count and the
// delete button on the same row.
constexpr float kTerrainNameWidth = 160.0f;

// How many tiles the list shows before it scrolls internally.
constexpr float kTileListRows = 10.0f;

// Every destructive control wears the same red so a confirmation prompt reads
// as belonging to the button that raised it.
constexpr ImVec4 kDestructiveColor{0.8f, 0.2f, 0.2f, 1.0f};

// The display name of a tileset in the catalog, or its ID when the catalog no
// longer holds it. A confirmation prompt has to name what it will destroy, and
// naming the wrong thing is worse than naming it awkwardly.
std::string TilesetDisplayName(const TilesetEditorModel& model, const std::string& tileset_id) {
  for (const auto& catalog_entry : model.tilesets()) {
    if (catalog_entry.second.id == tileset_id) return catalog_entry.second.name;
  }
  return tileset_id;
}

}  // namespace

absl::StatusOr<std::unique_ptr<TilesetPanel>> TilesetPanel::Create(GuiInterface* gui) {
  if (gui == nullptr) return absl::InvalidArgumentError("Gui cannot be null");
  return absl::WrapUnique(new TilesetPanel(gui));
}

TilesetPanel::TilesetPanel(GuiInterface* gui) : gui_(gui) {}

void TilesetPanel::CancelPendingConfirmations() {
  delete_tileset_prompt_.Disarm();
  delete_terrain_prompt_.Disarm();
  discard_edits_prompt_.Disarm();
}

absl::StatusOr<TilesetPanel::Action> TilesetPanel::RenderList(TilesetEditorModel& model) {
  if (gui_->Button("Create")) {
    CancelPendingConfirmations();
    model.BeginNewTileset();
    return Action::kNone;
  }
  gui_->SameLine();

  // Disabled rather than guarded: a button that is enabled, does nothing, and
  // says nothing teaches the user that the editor is broken. Nothing here is
  // selectable until a tileset is.
  const bool no_selection = !model.has_tileset_selection();
  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(no_selection);
    if (gui_->Button("Edit")) {
      CancelPendingConfirmations();
      RETURN_IF_ERROR(model.BeginEditingSelectedTileset());
      return Action::kNone;
    }
  }
  gui_->SameLine();

  ASSIGN_OR_RETURN(const Action action, RenderDeleteTilesetControl(model));
  if (action != Action::kNone) return action;

  if (ScopedListBox list_box = gui_->CreateScopedListBox("##Tilesets", ImVec2(-FLT_MIN, -FLT_MIN));
      list_box) {
    for (const auto& catalog_entry : model.tilesets()) {
      const Tileset& tileset = catalog_entry.second;
      const bool is_selected = model.selected_tileset_id() == tileset.id;
      if (gui_->Selectable(tileset.name.c_str(), is_selected,
                           ImGuiSelectableFlags_AllowDoubleClick)) {
        CancelPendingConfirmations();
        RETURN_IF_ERROR(model.SelectTileset(tileset.id));
        // Selecting and then pressing Edit is two steps for the only thing a
        // list entry is for. A double-click does both.
        if (gui_->IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          RETURN_IF_ERROR(model.BeginEditingSelectedTileset());
          return Action::kNone;
        }
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  return Action::kNone;
}

absl::StatusOr<TilesetPanel::Action> TilesetPanel::RenderDeleteTilesetControl(
    TilesetEditorModel& model) {
  const std::string& selected = model.selected_tileset_id();
  const std::string question =
      absl::StrCat("Delete '", TilesetDisplayName(model, selected), "'? This cannot be undone.");

  // The prompt only grows to a question once armed, so the disabled guard
  // belongs on the plain button and not on the answer.
  if (!delete_tileset_prompt_.armed()) {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(!model.has_tileset_selection());
    if (delete_tileset_prompt_.Render(*gui_, "Delete", selected, question, "Tileset")) {
      return Action::kDelete;
    }
    return Action::kNone;
  }

  if (delete_tileset_prompt_.Render(*gui_, "Delete", selected, question, "Tileset")) {
    return Action::kDelete;
  }
  return Action::kNone;
}

absl::StatusOr<TilesetPanel::Action> TilesetPanel::RenderDetails(TilesetEditorModel& model) {
  // Leaving is only destructive when there is something to lose, so a clean
  // tileset closes on the first click as it always did. The prompt is only
  // involved once there are edits to discard.
  if (discard_edits_prompt_.armed()) {
    if (discard_edits_prompt_.Render(*gui_, "Back", model.selected_tileset_id(),
                                     "Discard unsaved changes to this tileset?", "Back")) {
      model.CloseActiveTileset();
      return Action::kNone;
    }
  } else if (gui_->Button("Back")) {
    if (!model.has_unsaved_changes()) {
      CancelPendingConfirmations();
      model.CloseActiveTileset();
      return Action::kNone;
    }
    discard_edits_prompt_.Arm(model.selected_tileset_id());
  }
  gui_->SameLine();
  if (gui_->Button("Save")) {
    CancelPendingConfirmations();
    return Action::kSave;
  }

  // Back and Save stay outside the scroll region: they are how you leave and
  // how you keep your work, and a column too short to show everything must not
  // be able to hide them.
  gui_->Separator();
  ScopedChild body = gui_->CreateScopedChild("TilesetDetailsBody", ImVec2(0, 0), false);
  RETURN_IF_ERROR(RenderTilesetFields(model));

  gui_->Separator();
  return RenderTileList(model);
}

absl::Status TilesetPanel::RenderTilesetFields(TilesetEditorModel& model) {
  Tileset* tileset = model.active_tileset();
  if (tileset == nullptr) return absl::FailedPreconditionError("No tileset is being edited");

  gui_->InputText("Name", &tileset->name);

  std::string texture_preview = tileset->texture_id.empty() ? "(none)" : tileset->texture_id;
  if (const Texture* texture = model.active_texture(); texture != nullptr) {
    texture_preview = texture->name;
  }
  if (ScopedCombo combo = gui_->CreateScopedCombo("Texture", texture_preview.c_str()); combo) {
    for (const auto& catalog_entry : model.textures()) {
      const Texture& texture = catalog_entry.second;
      const bool is_selected = tileset->texture_id == texture.id;
      if (gui_->Selectable(texture.name.c_str(), is_selected)) {
        RETURN_IF_ERROR(model.SelectTexture(texture.id));
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  gui_->InputInt("Tile Width", &tileset->tile_width);
  gui_->InputInt("Tile Height", &tileset->tile_height);
  return absl::OkStatus();
}

absl::StatusOr<TilesetPanel::Action> TilesetPanel::RenderTileList(TilesetEditorModel& model) {
  Tileset* tileset = model.active_tileset();
  if (tileset == nullptr) return absl::FailedPreconditionError("No tileset is being edited");

  Action action = Action::kNone;

  gui_->Text("Tiles");
  gui_->SameLine();
  if (gui_->Button("Add")) RETURN_IF_ERROR(model.AddTile());
  gui_->SameLine();
  {
    // Still not confirmed, unlike deleting a tileset or a terrain, and for the
    // same reason: an unused tile is an Add and a click away, so asking would be
    // friction on the list you are actively editing.
    //
    // That reasoning only holds while nothing has painted the tile. Re-adding
    // one yields a fresh ID -- NextTileId is max+1 -- so a level that painted
    // the old ID cannot be put right by recreating the tile, and it stops
    // rendering entirely rather than losing one cell. The editor asks the Api
    // who has painted it and refuses; the friction lands on the tiles where the
    // risk actually is.
    ScopedDisabled disabled = gui_->CreateScopedDisabled(model.selected_tile() == nullptr);
    ScopedStyleColor style = gui_->CreateScopedStyleColor(ImGuiCol_Button, kDestructiveColor);
    if (gui_->Button("Delete##Tile")) action = Action::kDeleteTile;
  }

  // A fixed number of rows, not "whatever is left". Sizing this list against
  // the remaining space needs a prediction of everything below it, and a
  // prediction that has to stay in sync with four other render functions is how
  // the terrain controls ended up off the bottom of the window. The enclosing
  // child scrolls instead.
  const float list_height = kTileListRows * gui_->GetTextLineHeightWithSpacing();

  if (ScopedListBox list_box = gui_->CreateScopedListBox("##Tiles", ImVec2(-FLT_MIN, list_height));
      list_box) {
    for (const Tile& tile : tileset->tiles) {
      const std::string display =
          tile.name.empty() ? absl::StrFormat("Tile %d", tile.id) : tile.name;
      const bool is_selected = model.selected_tile_id() == tile.id;
      ScopedId id = gui_->CreateScopedId(tile.id);
      if (gui_->Selectable(display.c_str(), is_selected)) {
        RETURN_IF_ERROR(model.SelectTile(tile.id));
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  RETURN_IF_ERROR(RenderTerrainList(model));
  return action;
}

absl::Status TilesetPanel::RenderTerrainList(TilesetEditorModel& model) {
  Tileset* tileset = model.active_tileset();
  if (tileset == nullptr) return absl::FailedPreconditionError("No tileset is being edited");

  gui_->Separator();
  gui_->Text("Terrains");

  // Authoring a terrain lives in the Terrain tab, which has the room for a
  // preview. What remains here is what only makes sense beside the tiles:
  // recognising a terrain in artwork that already has tiles, and saying which
  // hand-placed tiles belong to one.
  if (gui_->Button("Detect##Terrain")) {
    absl::StatusOr<int> added = model.DetectTerrains();
    terrain_status_ = added.ok() ? absl::StrFormat("Detected %d terrain(s).", *added)
                                 : std::string(added.status().message());
  }

  if (!terrain_status_.empty()) gui_->TextWrapped("%s", terrain_status_.c_str());

  if (tileset->terrains.empty()) {
    gui_->TextDisabled("No terrains. Make one in the Terrain tab.");
    return absl::OkStatus();
  }

  for (Terrain& terrain : tileset->terrains) {
    ScopedId id = gui_->CreateScopedId(terrain.id);

    // The name is editable because importing cannot infer one: a manifest
    // describes geometry, not material, so every import arrives called
    // "Terrain". This name is also the brush's label in the level editor's
    // terrain palette, so leaving it fixed made two terrains in one tileset
    // indistinguishable while painting.
    gui_->SetNextItemWidth(kTerrainNameWidth);
    gui_->InputText("##TerrainName", &terrain.name);

    gui_->SameLine();
    gui_->Text("%s", absl::StrFormat("(%d masks)", terrain.rules.size()).c_str());
    gui_->SameLine();

    // Deleting a terrain throws away a whole 47-mask rule table that cannot be
    // rebuilt by hand, so it asks first. The row already names the terrain, so
    // the compact form with no question line is enough here.
    if (delete_terrain_prompt_.Render(*gui_, "Delete", absl::StrCat(terrain.id), "", "Terrain")) {
      RETURN_IF_ERROR(model.DeleteTerrain(terrain.id));
      // The loop is iterating the vector DeleteTerrain just erased from.
      return absl::OkStatus();
    }
  }

  return RenderTerrainMembership(model, *tileset);
}

absl::Status TilesetPanel::RenderTerrainMembership(TilesetEditorModel& model,
                                                   const Tileset& tileset) {
  const Tile* tile = model.selected_tile();
  if (tile == nullptr) return absl::OkStatus();

  gui_->Separator();
  gui_->Text("%s", absl::StrFormat("Selected tile: %s", tile->name).c_str());

  const std::optional<int> current = model.GetTileTerrainMembership(tile->id);
  std::string preview = "(not a terrain member)";
  for (const Terrain& terrain : tileset.terrains) {
    if (current.has_value() && terrain.id == *current) preview = terrain.name;
  }

  // Membership makes painted ground flow into hand-placed art such as slopes
  // instead of capping off with an edge against it.
  if (ScopedCombo combo = gui_->CreateScopedCombo("Member of##Terrain", preview.c_str()); combo) {
    if (gui_->Selectable("(not a terrain member)", !current.has_value())) {
      RETURN_IF_ERROR(model.SetTileTerrainMembership(tile->id, std::nullopt));
    }
    for (const Terrain& terrain : tileset.terrains) {
      const bool is_selected = current.has_value() && terrain.id == *current;
      ScopedId id = gui_->CreateScopedId(terrain.id);
      if (!gui_->Selectable(terrain.name.c_str(), is_selected)) continue;

      absl::Status status = model.SetTileTerrainMembership(tile->id, terrain.id);
      terrain_status_ = status.ok() ? "" : std::string(status.message());
    }
  }

  return absl::OkStatus();
}

}  // namespace zebes
