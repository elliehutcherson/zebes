#include "editor/tileset_editor/tileset_editor.h"

#include <cmath>

#include "SDL_render.h"
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
  ASSIGN_OR_RETURN(tileset_panel_, TilesetPanel::Create({.api = api_, .gui = gui_}));
  ASSIGN_OR_RETURN(tile_panel_, TilePanel::Create(gui_));
  return absl::OkStatus();
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
  if (!active_tileset_.has_value())
    return tileset_panel_->RenderList(active_tileset_);
  return tileset_panel_->RenderDetails(active_tileset_);
}

absl::Status TilesetEditor::RenderInspector() {
  if (!active_tileset_.has_value()) {
    gui_->Text("Select or create a tileset.");
    return absl::OkStatus();
  }
  Tile* tile = tileset_panel_->GetSelectedTile(*active_tileset_);
  if (tile == nullptr) {
    gui_->Text("Select or add a tile.");
    return absl::OkStatus();
  }
  return tile_panel_->RenderDetails(*tile);
}

absl::Status TilesetEditor::RenderViewport() {
  if (!active_tileset_.has_value() || active_tileset_->texture_id.empty()) {
    gui_->Text("Select a tileset with a texture to preview.");
    return absl::OkStatus();
  }

  // Resolve the SDL texture from the panel's cache.
  void* texture_handle = nullptr;
  for (const Texture& tex : tileset_panel_->GetTextures()) {
    if (tex.id == active_tileset_->texture_id) {
      texture_handle = SdlTextureHandleAdapter::ToNative(tex.texture_handle);
      break;
    }
  }

  if (texture_handle == nullptr) {
    gui_->Text("Texture not loaded.");
    return absl::OkStatus();
  }

  int tex_w = 0;
  int tex_h = 0;
  SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(texture_handle), nullptr, nullptr, &tex_w, &tex_h);

  const float tw = static_cast<float>(active_tileset_->tile_width);
  const float th = static_cast<float>(active_tileset_->tile_height);

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
    dl->AddImage(reinterpret_cast<ImTextureID>(texture_handle), img_min, img_max);

    // Draw grid and rulers on top of the texture.
    canvas_.DrawGrid();

    // Overlay: highlight the selected tile's source cell and collision shape.
    Tile* tile = tileset_panel_->GetSelectedTile(*active_tileset_);
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

      const float cell_x = std::floor(world.x / tw) * tw;
      const float cell_y = std::floor(world.y / th) * th;

      const bool in_bounds = cell_x >= 0 && cell_y >= 0 &&
                             (cell_x + tw) <= static_cast<float>(tex_w) &&
                             (cell_y + th) <= static_cast<float>(tex_h);
      if (in_bounds) {
        // Cyan hover highlight for the cell under the cursor.
        const ImVec2 h_min = canvas_.WorldToScreen({cell_x, cell_y});
        const ImVec2 h_max = canvas_.WorldToScreen({cell_x + tw, cell_y + th});
        dl->AddRectFilled(h_min, h_max, IM_COL32(0, 200, 255, 40));
        dl->AddRect(h_min, h_max, IM_COL32(0, 200, 255, 220), 0.0f, 0, 2.0f);

        if (gui_->IsItemClicked(ImGuiMouseButton_Left)) {
          tile->source_x = static_cast<int>(cell_x);
          tile->source_y = static_cast<int>(cell_y);
        }
      }
    }
  }

  float zoom = canvas_.GetZoom();
  canvas_.End();

  Tile* tile = tileset_panel_->GetSelectedTile(*active_tileset_);
  if (tile != nullptr)
    gui_->Text("Click atlas to set tile source  |  Zoom: %.2f", zoom);
  else
    gui_->Text("Zoom: %.2f", zoom);

  return absl::OkStatus();
}

}  // namespace zebes
