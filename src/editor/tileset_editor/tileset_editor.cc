#include "editor/tileset_editor/tileset_editor.h"

#include <utility>
#include <vector>

#include "SDL_render.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/canvas/tile_draw.h"
#include "editor/imgui_scoped.h"
#include "objects/tileset.h"
#include "platform/sdl/sdl_texture_handle.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<TilesetEditor>> TilesetEditor::Create(Api* api, GuiInterface* gui) {
  if (api == nullptr) return absl::InvalidArgumentError("Api is null.");
  if (gui == nullptr) return absl::InvalidArgumentError("GuiInterface is null.");
  auto editor = absl::WrapUnique(new TilesetEditor(api, gui));
  RETURN_IF_ERROR(editor->Init());
  return editor;
}

TilesetEditor::TilesetEditor(Api* api, GuiInterface* gui)
    : api_(api), gui_(gui), canvas_(Canvas::Options{.gui = gui}) {}

absl::Status TilesetEditor::Init() {
  ASSIGN_OR_RETURN(tileset_panel_, TilesetPanel::Create(gui_));
  ASSIGN_OR_RETURN(tile_panel_, TilePanel::Create(gui_));
  RefreshCatalogs();
  return absl::OkStatus();
}

void TilesetEditor::RefreshCatalogs() {
  model_.SetTilesets(api_->GetAllTilesets());

  absl::StatusOr<std::vector<Texture>> textures = api_->GetAllTextures();
  if (!textures.ok()) {
    LOG(ERROR) << "Failed to load textures: " << textures.status();
    model_.SetTextures({});
    return;
  }
  model_.SetTextures(std::move(*textures));
}

absl::Status TilesetEditor::SaveActiveTileset() {
  ASSIGN_OR_RETURN(Tileset request, model_.BuildSaveRequest());
  std::string saved_id = request.id;
  if (saved_id.empty()) {
    ASSIGN_OR_RETURN(saved_id, api_->CreateTileset(std::move(request)));
  } else {
    RETURN_IF_ERROR(api_->UpdateTileset(std::move(request)));
  }
  RETURN_IF_ERROR(model_.FinishSave(saved_id));
  RefreshCatalogs();
  return absl::OkStatus();
}

absl::Status TilesetEditor::DeleteSelectedTileset() {
  if (!model_.has_tileset_selection()) {
    return absl::FailedPreconditionError("No tileset is selected");
  }
  RETURN_IF_ERROR(api_->DeleteTileset(model_.selected_tileset_id()));
  model_.FinishDelete();
  RefreshCatalogs();
  return absl::OkStatus();
}

absl::Status TilesetEditor::HandlePanelAction(TilesetPanel::Action action) {
  switch (action) {
    case TilesetPanel::Action::kNone:
      return absl::OkStatus();
    case TilesetPanel::Action::kSave:
      return SaveActiveTileset();
    case TilesetPanel::Action::kDelete:
      return DeleteSelectedTileset();
  }
  return absl::InternalError("Unknown tileset panel action");
}

absl::Status TilesetEditor::Render() {
  if (error_message_.has_value()) {
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Error: %s", error_message_->c_str());
    gui_->SameLine();
    if (gui_->Button("Dismiss")) error_message_ = std::nullopt;
  }

  constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
  ScopedTable table = gui_->CreateScopedTable("TilesetEditorLayout", 3, kTableFlags);
  if (!table) return absl::OkStatus();

  gui_->TableSetupColumn("Navigator", ImGuiTableColumnFlags_WidthStretch, 0.2f);
  gui_->TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch, 0.6f);
  gui_->TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, 0.2f);

  gui_->TableNextColumn();
  if (absl::Status s = RenderNavigator(); !s.ok()) {
    error_message_ = s.message();
  }

  gui_->TableNextColumn();
  if (absl::Status s = RenderViewport(); !s.ok()) {
    error_message_ = s.message();
  }

  gui_->TableNextColumn();
  if (absl::Status s = RenderInspector(); !s.ok()) {
    error_message_ = s.message();
  }

  return absl::OkStatus();
}

absl::Status TilesetEditor::RenderNavigator() {
  absl::StatusOr<TilesetPanel::Action> action = model_.has_active_tileset()
                                                    ? tileset_panel_->RenderDetails(model_)
                                                    : tileset_panel_->RenderList(model_);
  if (!action.ok()) return action.status();
  return HandlePanelAction(*action);
}

absl::Status TilesetEditor::RenderInspector() {
  if (!model_.has_active_tileset()) {
    gui_->Text("Select or create a tileset.");
    return absl::OkStatus();
  }
  Tile* tile = model_.selected_tile();
  if (tile == nullptr) {
    gui_->Text("Select or add a tile.");
    return absl::OkStatus();
  }
  return tile_panel_->RenderDetails(*tile);
}

absl::Status TilesetEditor::RenderViewport() {
  Tileset* active_tileset = model_.active_tileset();
  if (active_tileset == nullptr || active_tileset->texture_id.empty()) {
    gui_->Text("Select a tileset with a texture to preview.");
    return absl::OkStatus();
  }

  const Texture* texture = model_.active_texture();
  SDL_Texture* native_texture =
      texture == nullptr ? nullptr : SdlTextureHandleAdapter::ToNative(texture->texture_handle);
  if (native_texture == nullptr) {
    gui_->Text("Texture not loaded.");
    return absl::OkStatus();
  }

  int tex_w = 0;
  int tex_h = 0;
  SDL_QueryTexture(native_texture, nullptr, nullptr, &tex_w, &tex_h);

  if (active_tileset->tile_width <= 0 || active_tileset->tile_height <= 0) {
    gui_->Text("Tile dimensions must be positive.");
    return absl::OkStatus();
  }
  const float tw = static_cast<float>(active_tileset->tile_width);
  const float th = static_cast<float>(active_tileset->tile_height);

  canvas_.SetWorldBounds({0, 0}, {static_cast<double>(tex_w), static_cast<double>(tex_h)});
  canvas_.SetGridSize(tw);

  ImVec2 canvas_size = gui_->GetContentRegionAvail();
  canvas_size.y -= 25.0f;  // Reserve space for the status bar.

  canvas_.Begin("TilesetCanvas", canvas_size, camera_);

  ImDrawList* dl = canvas_.GetDrawList();
  if (dl != nullptr) {
    // Draw the texture filling the entire world rect.
    ImVec2 img_min = canvas_.WorldToScreen({0, 0});
    ImVec2 img_max =
        canvas_.WorldToScreen({static_cast<double>(tex_w), static_cast<double>(tex_h)});
    dl->AddImage(reinterpret_cast<ImTextureID>(native_texture), img_min, img_max);

    // Draw grid and rulers on top of the texture.
    canvas_.DrawGrid();

    // Overlay: highlight the selected tile's source cell and collision shape.
    Tile* tile = model_.selected_tile();
    if (tile != nullptr) {
      const ImVec2 cell_min = canvas_.WorldToScreen(
          {static_cast<double>(tile->source_x), static_cast<double>(tile->source_y)});
      const ImVec2 cell_max = canvas_.WorldToScreen(
          {static_cast<double>(tile->source_x + tw), static_cast<double>(tile->source_y + th)});

      // Shape overlay first (drawn beneath the bounding box).
      if (tile->shape != TileShape::kNone) {
        DrawShapeOverlay(dl, cell_min, cell_max, tile->shape);
      }

      // Yellow bounding box around the current source cell.
      dl->AddRect(cell_min, cell_max, IM_COL32(255, 220, 50, 230), 0.0f, 0, 2.0f);
    }

    // Handle pan/zoom input. After this call, the canvas invisible button is
    // the last ImGui item, so IsItemHovered/IsItemClicked refer to it.
    canvas_.HandleInput();

    // Click-to-pick: when a tile is selected and the user clicks anywhere on
    // the atlas, snap the source coordinates to the clicked tile cell.
    if (tile != nullptr && gui_->IsItemHovered()) {
      const ImVec2 mouse = gui_->GetMousePos();
      const Vec world = canvas_.ScreenToWorld(mouse);

      absl::StatusOr<AtlasCell> cell = model_.CalculateAtlasCell(world.x, world.y, tex_w, tex_h);
      if (cell.ok()) {
        // Cyan hover highlight for the cell under the cursor.
        const ImVec2 h_min = canvas_.WorldToScreen(
            {static_cast<double>(cell->source_x), static_cast<double>(cell->source_y)});
        const ImVec2 h_max = canvas_.WorldToScreen(
            {static_cast<double>(cell->source_x) + tw, static_cast<double>(cell->source_y) + th});
        dl->AddRectFilled(h_min, h_max, IM_COL32(0, 200, 255, 40));
        dl->AddRect(h_min, h_max, IM_COL32(0, 200, 255, 220), 0.0f, 0, 2.0f);

        if (gui_->IsItemClicked(ImGuiMouseButton_Left)) {
          RETURN_IF_ERROR(model_.SetSelectedTileSource(*cell));
        }
      }
    }
  }

  float zoom = canvas_.GetZoom();
  canvas_.End();

  Tile* tile = model_.selected_tile();
  if (tile != nullptr)
    gui_->Text("Click atlas to set tile source  |  Zoom: %.2f", zoom);
  else
    gui_->Text("Zoom: %.2f", zoom);

  return absl::OkStatus();
}

}  // namespace zebes
