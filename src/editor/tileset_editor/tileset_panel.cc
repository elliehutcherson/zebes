#include "editor/tileset_editor/tileset_panel.h"

#include <algorithm>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_format.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<TilesetPanel>> TilesetPanel::Create(
    Options options) {
  if (options.api == nullptr)
    return absl::InvalidArgumentError("API can not be null.");
  if (options.gui == nullptr)
    return absl::InvalidArgumentError("Gui can not be null.");
  auto panel = absl::WrapUnique(new TilesetPanel(std::move(options)));
  RETURN_IF_ERROR(panel->Init());
  return panel;
}

TilesetPanel::TilesetPanel(Options options)
    : api_(*options.api), gui_(options.gui) {}

absl::Status TilesetPanel::Init() {
  RefreshTilesetCache();
  RefreshTextureCache();
  return absl::OkStatus();
}

void TilesetPanel::RefreshTilesetCache() {
  tileset_cache_ = api_.GetAllTilesets();
  std::sort(tileset_cache_.begin(), tileset_cache_.end(),
            [](const Tileset& a, const Tileset& b) { return a.name < b.name; });
  LOG(INFO) << "Loaded " << tileset_cache_.size() << " tilesets.";
}

void TilesetPanel::RefreshTextureCache() {
  absl::StatusOr<std::vector<Texture>> result = api_.GetAllTextures();
  if (!result.ok()) {
    LOG(ERROR) << "Failed to load textures: " << result.status();
    return;
  }
  texture_cache_ = std::move(*result);
  LOG(INFO) << "Loaded " << texture_cache_.size() << " textures.";
}

absl::Status TilesetPanel::HandleOp(std::optional<Tileset>& tileset, Op op) {
  // Guard: ops that require no active tileset.
  if (op == Op::kEdit || op == Op::kDelete) {
    if (tileset.has_value()) {
      return absl::InvalidArgumentError("Tileset must be null for Edit/Delete.");
    }
    if (selected_index_ < 0) {
      LOG(INFO) << "No tileset selected.";
      return absl::OkStatus();
    }
    if (selected_index_ >= static_cast<int>(tileset_cache_.size())) {
      return absl::InternalError("Selected index out of range.");
    }
  }

  switch (op) {
    case Op::kCreate:
      // Stage a new tileset locally without touching the backend. The id is
      // intentionally empty until kSave is called, which is how we distinguish
      // a pending-new tileset from an existing one being edited.
      tileset = Tileset{.name = "New Tileset", .tile_width = 16, .tile_height = 16};
      selected_tile_index_ = -1;
      break;
    case Op::kEdit:
      // Copy by value — never mutate through the cache pointer directly.
      // See MEMORY.md: "Rename Pattern Gotcha".
      tileset = tileset_cache_[selected_index_];
      selected_tile_index_ = -1;
      break;
    case Op::kSave: {
      if (!tileset.has_value()) {
        return absl::InvalidArgumentError("Tileset must not be null for Save.");
      }
      if (tileset->id.empty()) {
        // New tileset — create in the backend and assign the generated id.
        ASSIGN_OR_RETURN(std::string id, api_.CreateTileset(*tileset));
        tileset->id = std::move(id);
      } else {
        // Existing tileset — persist changes.
        RETURN_IF_ERROR(api_.UpdateTileset(*tileset));
      }
      // Refresh and restore the selection to point at the saved tileset.
      const std::string saved_id = tileset->id;
      RefreshTilesetCache();
      for (int i = 0; i < static_cast<int>(tileset_cache_.size()); ++i) {
        if (tileset_cache_[i].id == saved_id) {
          selected_index_ = i;
          break;
        }
      }
      break;
    }
    case Op::kDelete:
      RETURN_IF_ERROR(api_.DeleteTileset(tileset_cache_[selected_index_].id));
      RefreshTilesetCache();
      selected_index_ = -1;
      selected_tile_index_ = -1;
      break;
    case Op::kBack:
      tileset.reset();
      selected_tile_index_ = -1;
      break;
    case Op::kTileAdd: {
      if (!tileset.has_value()) {
        return absl::InvalidArgumentError("Tileset must not be null for TileAdd.");
      }
      // Tile ID 0 is reserved as "empty". Assign max(existing ids) + 1.
      int next_id = 1;
      for (const Tile& t : tileset->tiles) {
        next_id = std::max(next_id, t.id + 1);
      }
      tileset->tiles.push_back(Tile{.id = next_id});
      selected_tile_index_ = static_cast<int>(tileset->tiles.size()) - 1;
      break;
    }
    case Op::kTileDelete:
      if (!tileset.has_value()) {
        return absl::InvalidArgumentError("Tileset must not be null for TileDelete.");
      }
      if (selected_tile_index_ < 0 ||
          selected_tile_index_ >= static_cast<int>(tileset->tiles.size())) {
        LOG(INFO) << "No tile selected, not deleting.";
        return absl::OkStatus();
      }
      tileset->tiles.erase(tileset->tiles.begin() + selected_tile_index_);
      selected_tile_index_ = -1;
      break;
  }
  return absl::OkStatus();
}

absl::Status TilesetPanel::RenderList(std::optional<Tileset>& tileset) {
  if (gui_->Button("Create")) {
    RETURN_IF_ERROR(HandleOp(tileset, Op::kCreate));
  }
  gui_->SameLine();

  if (gui_->Button("Edit")) {
    RETURN_IF_ERROR(HandleOp(tileset, Op::kEdit));
  }
  gui_->SameLine();

  {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete")) {
      RETURN_IF_ERROR(HandleOp(tileset, Op::kDelete));
    }
  }

  if (ScopedListBox list_box =
          gui_->CreateScopedListBox("##Tilesets", ImVec2(-FLT_MIN, -FLT_MIN));
      list_box) {
    for (int i = 0; i < static_cast<int>(tileset_cache_.size()); ++i) {
      const bool is_selected = (selected_index_ == i);
      if (gui_->Selectable(tileset_cache_[i].name.c_str(), is_selected)) {
        selected_index_ = i;
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  return absl::OkStatus();
}

absl::Status TilesetPanel::RenderDetails(std::optional<Tileset>& tileset) {
  // Caller guarantees tileset.has_value().
  Tileset& ts = *tileset;

  if (gui_->Button("Back")) {
    RETURN_IF_ERROR(HandleOp(tileset, Op::kBack));
    // tileset is now nullopt — stop rendering to avoid dangling reference.
    return absl::OkStatus();
  }
  gui_->SameLine();
  if (gui_->Button("Save")) {
    RETURN_IF_ERROR(HandleOp(tileset, Op::kSave));
  }

  gui_->Separator();
  RETURN_IF_ERROR(RenderTilesetFields(ts));

  gui_->Separator();
  RETURN_IF_ERROR(RenderTileList(tileset));

  return absl::OkStatus();
}

absl::Status TilesetPanel::RenderTilesetFields(Tileset& ts) {
  gui_->InputText("Name", &ts.name);

  // Resolve the current texture display name for the combo preview.
  const char* texture_preview = ts.texture_id.empty() ? "(none)" : ts.texture_id.c_str();
  for (const Texture& tex : texture_cache_) {
    if (tex.id == ts.texture_id) {
      texture_preview = tex.name.c_str();
      break;
    }
  }
  if (ScopedCombo combo = gui_->CreateScopedCombo("Texture", texture_preview); combo) {
    for (const Texture& tex : texture_cache_) {
      bool is_selected = (ts.texture_id == tex.id);
      if (gui_->Selectable(tex.name.c_str(), is_selected)) {
        ts.texture_id = tex.id;
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  gui_->InputInt("Tile Width", &ts.tile_width);
  gui_->InputInt("Tile Height", &ts.tile_height);

  return absl::OkStatus();
}

absl::Status TilesetPanel::RenderTileList(std::optional<Tileset>& tileset) {
  Tileset& ts = *tileset;

  gui_->Text("Tiles");
  gui_->SameLine();
  if (gui_->Button("Add")) {
    RETURN_IF_ERROR(HandleOp(tileset, Op::kTileAdd));
  }
  gui_->SameLine();
  {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete##Tile")) {
      RETURN_IF_ERROR(HandleOp(tileset, Op::kTileDelete));
    }
  }

  if (ScopedListBox lb = gui_->CreateScopedListBox("##Tiles", ImVec2(-FLT_MIN, -FLT_MIN)); lb) {
    for (int i = 0; i < static_cast<int>(ts.tiles.size()); ++i) {
      const Tile& t = ts.tiles[i];
      std::string display =
          t.name.empty() ? absl::StrFormat("Tile %d", t.id) : t.name;
      const bool is_selected = (selected_tile_index_ == i);
      ScopedId id = gui_->CreateScopedId(i);
      if (gui_->Selectable(display.c_str(), is_selected)) {
        selected_tile_index_ = i;
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  return absl::OkStatus();
}

Tile* TilesetPanel::GetSelectedTile(Tileset& tileset) {
  if (selected_tile_index_ < 0 ||
      selected_tile_index_ >= static_cast<int>(tileset.tiles.size())) {
    return nullptr;
  }
  return &tileset.tiles[selected_tile_index_];
}

}  // namespace zebes
