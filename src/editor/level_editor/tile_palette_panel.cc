#include "editor/level_editor/tile_palette_panel.h"

#include <cmath>

#include "absl/memory/memory.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "editor/palette_ui.h"
#include "editor/texture_preview.h"
#include "imgui.h"
#include "objects/texture.h"

namespace zebes {

namespace {

// Display size of each tile thumbnail button in pixels.
constexpr float kThumbnailSize = 40.0f;
// Padding between thumbnail buttons.
constexpr float kThumbnailPad = 4.0f;

// Draws one tile's thumbnail and its selection or hover border. thumb_w/thumb_h
// are display dimensions and tile_w/tile_h are the source atlas dimensions for
// UV sampling; the two differ when the level's tile render size is not square.
void DrawTileThumbnail(ImDrawList* dl, ImVec2 cursor, const Tile& tile, const AtlasBinding& atlas,
                       float tile_w, float tile_h, float thumb_w, float thumb_h, bool is_selected,
                       bool is_hovered, float overlay_opacity) {
  ImVec2 btn_max = ImVec2(cursor.x + thumb_w, cursor.y + thumb_h);

  if (atlas.IsValid()) {
    float u0 = static_cast<float>(tile.source_x) / atlas.width;
    float v0 = static_cast<float>(tile.source_y) / atlas.height;
    float u1 = static_cast<float>(tile.source_x + tile_w) / atlas.width;
    float v1 = static_cast<float>(tile.source_y + tile_h) / atlas.height;
    dl->AddImage(atlas.texture_id, cursor, btn_max, ImVec2(u0, v0), ImVec2(u1, v1));
  } else {
    dl->AddRectFilled(cursor, btn_max, IM_COL32(80, 80, 80, 200));
  }

  if (overlay_opacity > 0.0f) {
    dl->AddRectFilled(cursor, btn_max,
                      IM_COL32(50, 100, 255, static_cast<uint8_t>(overlay_opacity * 255.0f)));
  }

  DrawPaletteItemFrame(*dl, cursor, btn_max, is_selected, is_hovered);
}

}  // namespace

absl::StatusOr<std::unique_ptr<TilePalettePanel>> TilePalettePanel::Create(Options options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null.");
  }
  if (options.gui == nullptr) {
    return absl::InvalidArgumentError("Gui must not be null.");
  }
  return absl::WrapUnique(new TilePalettePanel(std::move(options)));
}

TilePalettePanel::TilePalettePanel(Options options)
    : api_(*options.api), gui_(options.gui), texture_preview_(*options.gui) {}

absl::Status TilePalettePanel::HandleTileClick(int tile_id, bool is_selected) {
  if (is_selected) {
    selected_tile_id_.reset();
    selected_tile_ = nullptr;
    return absl::OkStatus();
  }
  if (selected_tileset_ == nullptr) {
    return absl::FailedPreconditionError("Cannot select a tile without a tileset.");
  }
  for (const Tile& t : selected_tileset_->tiles) {
    if (t.id != tile_id) continue;
    selected_tile_id_ = tile_id;
    selected_tile_ = &t;
    return absl::OkStatus();
  }
  return absl::NotFoundError("Selected tile is missing from the active tileset.");
}

absl::Status TilePalettePanel::RenderTileGrid(const AtlasBinding& atlas, int tile_render_w,
                                              int tile_render_h, float overlay_opacity) {
  auto child = ScopedChild(gui_, "TileGrid", ImVec2(0, 0), false);
  if (!child) return absl::OkStatus();

  const float tile_w = static_cast<float>(selected_tileset_->tile_width);
  const float tile_h = static_cast<float>(selected_tileset_->tile_height);

  // Scale thumbnail so the longest render dimension maps to kThumbnailSize.
  // This makes the palette reflect the tile's rendered shape in the world.
  const float render_max = static_cast<float>(std::max(tile_render_w, tile_render_h));
  const float thumb_w = kThumbnailSize * (static_cast<float>(tile_render_w) / render_max);
  const float thumb_h = kThumbnailSize * (static_cast<float>(tile_render_h) / render_max);
  ASSIGN_OR_RETURN(
      const PaletteGridLayout layout,
      CalculatePaletteGridLayout(gui_->GetContentRegionAvail().x, thumb_w, kThumbnailPad));

  int col = 0;
  for (const Tile& tile : selected_tileset_->tiles) {
    const bool is_selected = (selected_tile_ != nullptr && selected_tile_->id == tile.id);

    ScopedId scoped_id = gui_->CreateScopedId(tile.id);

    ImVec2 cursor = gui_->GetCursorScreenPos();
    gui_->InvisibleButton("##tile", ImVec2(thumb_w, thumb_h));
    const bool clicked = gui_->IsItemClicked(0);
    const bool hovered = gui_->IsItemHovered();

    ImDrawList* dl = gui_->GetWindowDrawList();
    if (dl != nullptr) {
      DrawTileThumbnail(dl, cursor, tile, atlas, tile_w, tile_h, thumb_w, thumb_h, is_selected,
                        hovered, overlay_opacity);
    }

    if (clicked) {
      RETURN_IF_ERROR(HandleTileClick(tile.id, is_selected));
    }

    ++col;
    if (layout.ContinueRowAfter(col)) {
      gui_->SameLine();
    }
  }
  return absl::OkStatus();
}

absl::Status TilePalettePanel::Render(int tile_render_width, int tile_render_height) {
  ASSIGN_OR_RETURN(const TilesetSelectorResult selection,
                   tileset_selector_.Render(api_, *gui_, "Tileset##tile_palette", "tileset_"));
  selected_tileset_ = selection.tileset;
  if (selection.selection_changed) {
    selected_tile_id_.reset();
    selected_tile_ = nullptr;
  }

  selected_tile_ = nullptr;
  if (selected_tileset_ != nullptr && selected_tile_id_.has_value()) {
    for (const Tile& tile : selected_tileset_->tiles) {
      if (tile.id != *selected_tile_id_) continue;
      selected_tile_ = &tile;
      break;
    }
    if (selected_tile_ == nullptr) selected_tile_id_.reset();
  }

  gui_->SameLine();
  gui_->Checkbox("Show Frame", &show_tile_frame_);
  gui_->SameLine();
  gui_->Checkbox("Show Collision", &show_tile_collision_);
  gui_->SliderFloat("Tile Overlay", &tile_overlay_opacity_, /*v_min=*/0.0f, /*v_max=*/1.0f);

  if (selected_tileset_ == nullptr) {
    gui_->TextDisabled(selection.catalog_empty ? "No tilesets loaded." : "Select a tileset above.");
    return absl::OkStatus();
  }

  // An unset or unloaded texture is a valid authoring state; the grid falls back
  // to placeholder swatches rather than failing the frame.
  AtlasBinding atlas;
  if (!selected_tileset_->texture_id.empty()) {
    ASSIGN_OR_RETURN(TextureHandle handle, api_.GetTextureHandle(selected_tileset_->texture_id));
    ASSIGN_OR_RETURN(atlas, texture_preview_.BindAtlas(handle));
  }

  return RenderTileGrid(atlas, tile_render_width, tile_render_height, tile_overlay_opacity_);
}

}  // namespace zebes
