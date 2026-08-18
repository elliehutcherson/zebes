#include "editor/tileset_editor/tileset_editor.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "SDL_render.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
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

absl::Status TilesetEditor::DeleteSelectedTile() {
  const Tile* tile = model_.selected_tile();
  if (tile == nullptr) return absl::FailedPreconditionError("No tile is selected");

  // Checked against the saved tilesets and levels rather than the copy being
  // edited: what a level painted is the ID on disk, and that is what stops
  // resolving if this tile goes.
  RETURN_IF_ERROR(api_->CheckTileDeletable(model_.selected_tileset_id(), tile->id));
  return model_.DeleteSelectedTile();
}

absl::Status TilesetEditor::HandlePanelAction(TilesetPanel::Action action) {
  switch (action) {
    case TilesetPanel::Action::kNone:
      return absl::OkStatus();
    case TilesetPanel::Action::kSave:
      return SaveActiveTileset();
    case TilesetPanel::Action::kDelete:
      return DeleteSelectedTileset();
    case TilesetPanel::Action::kDeleteTile:
      return DeleteSelectedTile();
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
  ASSIGN_OR_RETURN(TilesetPanel::Action action, model_.has_active_tileset()
                                                    ? tileset_panel_->RenderDetails(model_)
                                                    : tileset_panel_->RenderList(model_));
  return HandlePanelAction(action);
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
  TextureHandle handle;
  if (texture != nullptr) {
    ASSIGN_OR_RETURN(handle, api_->GetTextureHandle(texture->id));
  }
  SDL_Texture* native_texture = SdlTextureHandleAdapter::ToNative(handle);
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

    RETURN_IF_ERROR(HandleAtlasInteraction(dl, tex_w, tex_h));
  }

  float zoom = canvas_.GetZoom();
  canvas_.End();

  if (!viewport_status_.empty()) {
    gui_->Text("%s  |  Zoom: %.2f", viewport_status_.c_str(), zoom);
  } else {
    gui_->Text("Click a cell to set the selected tile's source, drag to add tiles  |  Zoom: %.2f",
               zoom);
  }

  return absl::OkStatus();
}

void TilesetEditor::DrawCellRegion(ImDrawList* draw_list, AtlasCell first, AtlasCell last,
                                   ImU32 fill, ImU32 border) const {
  const Tileset* tileset = model_.active_tileset();
  if (draw_list == nullptr || tileset == nullptr) return;

  const double tw = tileset->tile_width;
  const double th = tileset->tile_height;
  const double min_x = std::min(first.source_x, last.source_x);
  const double min_y = std::min(first.source_y, last.source_y);
  const double max_x = std::max(first.source_x, last.source_x) + tw;
  const double max_y = std::max(first.source_y, last.source_y) + th;

  const ImVec2 screen_min = canvas_.WorldToScreen({min_x, min_y});
  const ImVec2 screen_max = canvas_.WorldToScreen({max_x, max_y});
  draw_list->AddRectFilled(screen_min, screen_max, fill);
  draw_list->AddRect(screen_min, screen_max, border, 0.0f, 0, 2.0f);
}

absl::Status TilesetEditor::HandleAtlasInteraction(ImDrawList* draw_list, int texture_width,
                                                   int texture_height) {
  std::optional<AtlasCell> hovered;
  if (gui_->IsItemHovered()) {
    const Vec world = canvas_.ScreenToWorld(gui_->GetMousePos());
    absl::StatusOr<AtlasCell> cell =
        model_.CalculateAtlasCell(world.x, world.y, texture_width, texture_height);
    // A cursor off the edge of the atlas is not an error; there is simply no
    // cell there to act on.
    if (cell.ok()) hovered = *cell;
  }

  if (gui_->IsItemClicked(ImGuiMouseButton_Left) && hovered.has_value()) {
    drag_anchor_ = hovered;
    drag_current_ = hovered;
  }

  // IsItemActive answers for the canvas button, which stays active for exactly
  // as long as the button is held. That is the only signal here that
  // distinguishes a drag still in progress from one the user has finished.
  if (drag_anchor_.has_value() && gui_->IsItemActive()) {
    if (hovered.has_value()) drag_current_ = hovered;
    DrawCellRegion(draw_list, *drag_anchor_, *drag_current_, IM_COL32(0, 200, 255, 40),
                   IM_COL32(0, 200, 255, 220));
    return absl::OkStatus();
  }

  if (drag_anchor_.has_value()) {
    const AtlasCell anchor = *drag_anchor_;
    const AtlasCell end = *drag_current_;
    drag_anchor_.reset();
    drag_current_.reset();
    return CommitAtlasGesture(anchor, end);
  }

  if (hovered.has_value()) {
    DrawCellRegion(draw_list, *hovered, *hovered, IM_COL32(0, 200, 255, 40),
                   IM_COL32(0, 200, 255, 220));
  }
  return absl::OkStatus();
}

absl::Status TilesetEditor::CommitAtlasGesture(AtlasCell anchor, AtlasCell end) {
  if (anchor == end) {
    // One cell is the gesture this viewport has always had: re-point the
    // selected tile. With nothing selected there is nothing to re-point, and
    // silently adding a tile instead would make a stray click destructive.
    if (model_.selected_tile() == nullptr) {
      viewport_status_ = "Select a tile first, or drag to add tiles";
      return absl::OkStatus();
    }
    RETURN_IF_ERROR(model_.SetSelectedTileSource(anchor));
    viewport_status_ = "Set tile source";
    return absl::OkStatus();
  }

  ASSIGN_OR_RETURN(const int added, model_.AddTilesForRegion(anchor, end));
  viewport_status_ = added == 0 ? "Every cell in that region already has a tile"
                                : absl::StrFormat("Added %d tile(s)", added);
  return absl::OkStatus();
}

}  // namespace zebes
