#include "editor/level_editor/terrain_palette_panel.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"
#include "objects/texture.h"

namespace zebes {
namespace {

// Display size of a terrain swatch in pixels.
constexpr float kSwatchSize = 48.0f;
// Gap between a swatch and its label.
constexpr float kSwatchPad = 8.0f;

// The fully surrounded mask. Its tile is the interior of a filled region, which
// reads as the material itself rather than one of its edges.
constexpr uint8_t kSolidMask = 255;

// Returns the tile a terrain paints in the middle of a solid region, or null
// when the terrain has no rule for it.
const Tile* FindSwatchTile(const Tileset& tileset, const Terrain& terrain) {
  for (const TerrainRule& rule : terrain.rules) {
    if (rule.mask != kSolidMask || rule.variants.empty()) continue;
    for (const Tile& tile : tileset.tiles) {
      if (tile.id == rule.variants.front().tile_id) return &tile;
    }
  }
  return nullptr;
}

void DrawSwatch(ImDrawList* draw_list, ImVec2 cursor, const AtlasBinding& atlas, const Tile* tile,
                const Tileset& tileset, bool is_selected, bool is_hovered) {
  const ImVec2 max = ImVec2(cursor.x + kSwatchSize, cursor.y + kSwatchSize);

  if (atlas.IsValid() && tile != nullptr) {
    const float u0 = static_cast<float>(tile->source_x) / atlas.width;
    const float v0 = static_cast<float>(tile->source_y) / atlas.height;
    const float u1 = static_cast<float>(tile->source_x + tileset.tile_width) / atlas.width;
    const float v1 = static_cast<float>(tile->source_y + tileset.tile_height) / atlas.height;
    draw_list->AddImage(atlas.texture_id, cursor, max, ImVec2(u0, v0), ImVec2(u1, v1));
  } else {
    draw_list->AddRectFilled(cursor, max, IM_COL32(80, 80, 80, 200));
  }

  if (is_hovered) draw_list->AddRect(cursor, max, IM_COL32(200, 200, 200, 180), 0.0f, 0, 1.0f);
  if (is_selected) draw_list->AddRect(cursor, max, IM_COL32(60, 120, 255, 255), 0.0f, 0, 2.0f);
}

}  // namespace

absl::StatusOr<std::unique_ptr<TerrainPalettePanel>> TerrainPalettePanel::Create(Options options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null.");
  }
  if (options.gui == nullptr) {
    return absl::InvalidArgumentError("Gui must not be null.");
  }
  return absl::WrapUnique(new TerrainPalettePanel(std::move(options)));
}

TerrainPalettePanel::TerrainPalettePanel(Options options)
    : api_(*options.api), gui_(options.gui), texture_preview_(*options.gui) {}

std::optional<int> TerrainPalettePanel::GetSelectedTerrainId() const {
  return selected_terrain_id_;
}

absl::Status TerrainPalettePanel::RenderTerrainList(const AtlasBinding& atlas) {
  ScopedChild child = ScopedChild(gui_, "TerrainList", ImVec2(0, 0), false);
  if (!child) return absl::OkStatus();

  if (selected_tileset_->terrains.empty()) {
    gui_->TextDisabled("This tileset defines no terrains.");
    gui_->TextWrapped(
        "Import one in the Tileset Editor, or generate an atlas with scripts/compose_blob47.");
    return absl::OkStatus();
  }

  for (const Terrain& terrain : selected_tileset_->terrains) {
    const bool is_selected =
        selected_terrain_id_.has_value() && *selected_terrain_id_ == terrain.id;

    ScopedId scoped_id = gui_->CreateScopedId(terrain.id);
    const ImVec2 cursor = gui_->GetCursorScreenPos();
    gui_->InvisibleButton("##terrain", ImVec2(kSwatchSize, kSwatchSize));
    const bool clicked = gui_->IsItemClicked(0);
    const bool hovered = gui_->IsItemHovered();

    ImDrawList* draw_list = gui_->GetWindowDrawList();
    if (draw_list != nullptr) {
      DrawSwatch(draw_list, cursor, atlas, FindSwatchTile(*selected_tileset_, terrain),
                 *selected_tileset_, is_selected, hovered);
    }

    gui_->SameLine();
    gui_->SetCursorPosX(gui_->GetCursorPosX() + kSwatchPad);
    gui_->AlignTextToFramePadding();
    gui_->Text("%s", terrain.name.c_str());

    // Clicking the selected terrain deselects it, matching the tile palette.
    if (clicked) {
      selected_terrain_id_ = is_selected ? std::nullopt : std::optional<int>(terrain.id);
    }
  }
  return absl::OkStatus();
}

absl::Status TerrainPalettePanel::Render() {
  std::vector<Tileset> tilesets = api_.GetAllTilesets();
  const char* preview =
      (selected_tileset_ != nullptr) ? selected_tileset_->name.c_str() : "(none)";

  if (ScopedCombo combo = gui_->CreateScopedCombo("Tileset##terrain_palette", preview); combo) {
    for (const Tileset& tileset : tilesets) {
      const bool is_selected =
          selected_tileset_ != nullptr && selected_tileset_->id == tileset.id;
      const std::string label = absl::StrCat(
          tileset.name.empty() ? "(unnamed tileset)" : tileset.name, "##terrain_ts_", tileset.id);
      if (!gui_->Selectable(label.c_str(), is_selected)) continue;

      ASSIGN_OR_RETURN(Tileset * stable, api_.GetTileset(tileset.id));
      selected_tileset_ = stable;
      selected_terrain_id_.reset();
    }
  }

  if (selected_tileset_ == nullptr) {
    gui_->TextDisabled(tilesets.empty() ? "No tilesets loaded." : "Select a tileset above.");
    return absl::OkStatus();
  }

  // An unset or unloaded texture is a valid authoring state; swatches fall back
  // to placeholders rather than failing the frame.
  AtlasBinding atlas;
  if (!selected_tileset_->texture_id.empty()) {
    ASSIGN_OR_RETURN(TextureHandle handle,
                     api_.GetTextureHandle(selected_tileset_->texture_id));
    ASSIGN_OR_RETURN(atlas, texture_preview_.BindAtlas(handle));
  }

  return RenderTerrainList(atlas);
}

}  // namespace zebes
