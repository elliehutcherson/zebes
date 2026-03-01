#include "editor/level_editor/tile_palette_panel.h"

#include <cmath>

#include "SDL_render.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"
#include "objects/texture.h"

namespace zebes {

namespace {

// Display size of each tile thumbnail button in pixels.
constexpr float kThumbnailSize = 40.0f;
// Padding between thumbnail buttons.
constexpr float kThumbnailPad = 4.0f;

// Draws the thumbnail image and selection/hover borders for a single tile.
// thumb_w/thumb_h are the display dimensions of the button (may differ from source size when
// the level's tile render dimensions have a non-square aspect ratio).
// tile_w/tile_h are the source atlas dimensions used for UV sampling.
// Isolated here to keep the per-tile loop body flat.
void DrawTileThumbnail(ImDrawList* dl, ImVec2 cursor, const Tile& tile, void* sdl_texture,
                       int tex_w, int tex_h, float tile_w, float tile_h, float thumb_w,
                       float thumb_h, bool is_selected, bool is_hovered) {
  ImVec2 btn_max = ImVec2(cursor.x + thumb_w, cursor.y + thumb_h);

  if (sdl_texture != nullptr && tex_w > 0 && tex_h > 0) {
    float u0 = static_cast<float>(tile.source_x) / tex_w;
    float v0 = static_cast<float>(tile.source_y) / tex_h;
    float u1 = static_cast<float>(tile.source_x + tile_w) / tex_w;
    float v1 = static_cast<float>(tile.source_y + tile_h) / tex_h;
    dl->AddImage(reinterpret_cast<ImTextureID>(sdl_texture), cursor, btn_max, ImVec2(u0, v0),
                 ImVec2(u1, v1));
  } else {
    dl->AddRectFilled(cursor, btn_max, IM_COL32(80, 80, 80, 200));
  }

  if (is_hovered) dl->AddRect(cursor, btn_max, IM_COL32(200, 200, 200, 180), 0.0f, 0, 1.0f);
  if (is_selected) dl->AddRect(cursor, btn_max, IM_COL32(60, 120, 255, 255), 0.0f, 0, 2.0f);
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

TilePalettePanel::TilePalettePanel(Options options) : api_(*options.api), gui_(options.gui) {}

absl::Status TilePalettePanel::HandleTileClick(int tile_id, bool is_selected) {
  if (is_selected) {
    selected_tile_ = nullptr;
    return absl::OkStatus();
  }
  ASSIGN_OR_RETURN(Tileset* ts, api_.GetTileset(selected_tileset_->id));
  selected_tileset_ = ts;
  for (Tile& t : ts->tiles) {
    if (t.id != tile_id) continue;
    selected_tile_ = &t;
    return absl::OkStatus();
  }
  return absl::OkStatus();
}

absl::Status TilePalettePanel::RenderTileGrid(void* sdl_texture, int tex_w, int tex_h,
                                              int tile_render_w, int tile_render_h) {
  auto child = ScopedChild(gui_, "TileGrid", ImVec2(0, 0), false);
  if (!child) return absl::OkStatus();

  const float tile_w = static_cast<float>(selected_tileset_->tile_width);
  const float tile_h = static_cast<float>(selected_tileset_->tile_height);

  // Scale thumbnail so the longest render dimension maps to kThumbnailSize.
  // This makes the palette reflect the tile's rendered shape in the world.
  const float render_max = static_cast<float>(std::max(tile_render_w, tile_render_h));
  const float thumb_w = kThumbnailSize * (static_cast<float>(tile_render_w) / render_max);
  const float thumb_h = kThumbnailSize * (static_cast<float>(tile_render_h) / render_max);
  const float step_w = thumb_w + kThumbnailPad;
  const int tiles_per_row = std::max(1, static_cast<int>(gui_->GetContentRegionAvail().x / step_w));

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
      DrawTileThumbnail(dl, cursor, tile, sdl_texture, tex_w, tex_h, tile_w, tile_h, thumb_w,
                        thumb_h, is_selected, hovered);
    }

    if (clicked) {
      RETURN_IF_ERROR(HandleTileClick(tile.id, is_selected));
    }

    ++col;
    if (col % tiles_per_row != 0) {
      gui_->SameLine();
    } else {
      col = 0;
    }
  }
  return absl::OkStatus();
}

absl::Status TilePalettePanel::Render(int tile_render_width, int tile_render_height) {
  // --- Tileset selector ---
  std::vector<Tileset> tilesets = api_.GetAllTilesets();
  const char* preview =
      (selected_tileset_ != nullptr) ? selected_tileset_->name.c_str() : "(none)";

  if (ScopedCombo combo = gui_->CreateScopedCombo("Tileset##tile_palette", preview); combo) {
    for (const Tileset& ts : tilesets) {
      bool is_selected = (selected_tileset_ != nullptr && selected_tileset_->id == ts.id);
      if (!gui_->Selectable(ts.name.c_str(), is_selected)) continue;

      absl::StatusOr<Tileset*> ts_ptr = api_.GetTileset(ts.id);
      if (!ts_ptr.ok()) {
        LOG(ERROR) << "Couldn't find tileset: " << ts.id;
        continue;
      }
      selected_tileset_ = *ts_ptr;
      selected_tile_ = nullptr;
    }
  }

  gui_->SameLine();
  gui_->Checkbox("Show Frame", &show_tile_frame_);
  gui_->SameLine();
  gui_->Checkbox("Show Collision", &show_tile_collision_);

  if (selected_tileset_ == nullptr) {
    gui_->TextDisabled(tilesets.empty() ? "No tilesets loaded." : "Select a tileset above.");
    return absl::OkStatus();
  }

  // --- Resolve texture ---
  void* sdl_texture = nullptr;
  int tex_w = 0;
  int tex_h = 0;

  if (!selected_tileset_->texture_id.empty()) {
    ASSIGN_OR_RETURN(Texture* tex, api_.GetTexture(selected_tileset_->texture_id));
    if (tex != nullptr && tex->sdl_texture != nullptr) {
      sdl_texture = tex->sdl_texture;
      SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(sdl_texture), nullptr, nullptr, &tex_w,
                       &tex_h);
    }
  }

  return RenderTileGrid(sdl_texture, tex_w, tex_h, tile_render_width, tile_render_height);
}

}  // namespace zebes
