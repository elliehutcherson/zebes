#include "editor/tileset_editor/tileset_panel.h"

#include <cstring>
#include <fstream>

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

// Buffer size for the manifest path input field.
constexpr size_t kManifestPathCapacity = 512;

// Reads a compose_blob47 manifest from disk and adds its terrain to the model.
absl::Status ImportTerrainFromFile(TilesetEditorModel& model, const std::string& path) {
  if (path.empty()) return absl::InvalidArgumentError("Manifest path is empty.");

  std::ifstream file(path);
  if (!file.is_open()) {
    return absl::NotFoundError(absl::StrCat("Cannot open manifest: ", path));
  }
  const std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  return model.ImportTerrainManifest(contents);
}

}  // namespace

absl::StatusOr<std::unique_ptr<TilesetPanel>> TilesetPanel::Create(GuiInterface* gui) {
  if (gui == nullptr) return absl::InvalidArgumentError("Gui cannot be null");
  return absl::WrapUnique(new TilesetPanel(gui));
}

TilesetPanel::TilesetPanel(GuiInterface* gui) : gui_(gui) {}

absl::StatusOr<TilesetPanel::Action> TilesetPanel::RenderList(TilesetEditorModel& model) {
  if (gui_->Button("Create")) model.BeginNewTileset();
  gui_->SameLine();

  if (gui_->Button("Edit") && model.has_tileset_selection()) {
    RETURN_IF_ERROR(model.BeginEditingSelectedTileset());
  }
  gui_->SameLine();

  {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete") && model.has_tileset_selection()) return Action::kDelete;
  }

  if (ScopedListBox list_box = gui_->CreateScopedListBox("##Tilesets", ImVec2(-FLT_MIN, -FLT_MIN));
      list_box) {
    for (const auto& catalog_entry : model.tilesets()) {
      const Tileset& tileset = catalog_entry.second;
      const bool is_selected = model.selected_tileset_id() == tileset.id;
      if (gui_->Selectable(tileset.name.c_str(), is_selected)) {
        RETURN_IF_ERROR(model.SelectTileset(tileset.id));
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  return Action::kNone;
}

absl::StatusOr<TilesetPanel::Action> TilesetPanel::RenderDetails(TilesetEditorModel& model) {
  if (gui_->Button("Back")) {
    model.CloseActiveTileset();
    return Action::kNone;
  }
  gui_->SameLine();
  if (gui_->Button("Save")) return Action::kSave;

  gui_->Separator();
  RETURN_IF_ERROR(RenderTilesetFields(model));

  gui_->Separator();
  RETURN_IF_ERROR(RenderTileList(model));

  return Action::kNone;
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

absl::Status TilesetPanel::RenderTileList(TilesetEditorModel& model) {
  Tileset* tileset = model.active_tileset();
  if (tileset == nullptr) return absl::FailedPreconditionError("No tileset is being edited");

  gui_->Text("Tiles");
  gui_->SameLine();
  if (gui_->Button("Add")) RETURN_IF_ERROR(model.AddTile());
  gui_->SameLine();
  {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete##Tile")) RETURN_IF_ERROR(model.DeleteSelectedTile());
  }

  if (ScopedListBox list_box = gui_->CreateScopedListBox("##Tiles", ImVec2(-FLT_MIN, -FLT_MIN));
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
  return absl::OkStatus();
}

absl::Status TilesetPanel::RenderTerrainList(TilesetEditorModel& model) {
  Tileset* tileset = model.active_tileset();
  if (tileset == nullptr) return absl::FailedPreconditionError("No tileset is being edited");

  gui_->Separator();
  gui_->Text("Terrains");

  manifest_path_.resize(kManifestPathCapacity);
  gui_->InputText("Manifest##Terrain", manifest_path_.data(), manifest_path_.size());
  manifest_path_.resize(std::strlen(manifest_path_.c_str()));

  if (gui_->Button("Import##Terrain")) {
    absl::Status status = ImportTerrainFromFile(model, manifest_path_);
    terrain_status_ = status.ok() ? "Imported terrain." : std::string(status.message());
  }
  gui_->SameLine();
  if (gui_->Button("Detect##Terrain")) {
    absl::StatusOr<int> added = model.DetectTerrains();
    terrain_status_ = added.ok() ? absl::StrFormat("Detected %d terrain(s).", *added)
                                 : std::string(added.status().message());
  }

  if (!terrain_status_.empty()) gui_->TextWrapped("%s", terrain_status_.c_str());

  if (tileset->terrains.empty()) {
    gui_->TextDisabled("No terrains. Import a compose_blob47 manifest above.");
    return absl::OkStatus();
  }

  for (const Terrain& terrain : tileset->terrains) {
    ScopedId id = gui_->CreateScopedId(terrain.id);
    gui_->Text("%s", absl::StrFormat("%s (%d masks)", terrain.name, terrain.rules.size()).c_str());
    gui_->SameLine();
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete##Terrain")) RETURN_IF_ERROR(model.DeleteTerrain(terrain.id));
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
