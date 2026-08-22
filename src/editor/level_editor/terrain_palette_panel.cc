#include "editor/level_editor/terrain_palette_panel.h"

#include <string_view>
#include <vector>

#include "absl/memory/memory.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "editor/palette_ui.h"
#include "imgui.h"
#include "objects/texture.h"
#include "objects/tile_shape_geometry.h"
#include "terrain/terrain_placement.h"

namespace zebes {
namespace {

// Display size of a terrain swatch in pixels.
constexpr float kSwatchSize = 48.0f;
// Gap between a swatch and its label.
constexpr float kSwatchPad = 8.0f;

// Display size of a shape glyph in the picker grid. Smaller than a terrain
// swatch because there are twenty-five of them and a silhouette stays legible
// far below the size artwork needs.
constexpr float kShapeSize = 32.0f;

// Draws a shape's solid region as a filled polygon.
//
// A silhouette rather than a thumbnail of artwork, because this picker chooses
// collision geometry and the artwork follows from it. What a cell actually
// looks like depends on its whole neighbourhood, so any single thumbnail would
// have to invent one and would then disagree with what lands. The geometry does
// not: it is exactly what the click places. The cursor ghost already shows the
// real artwork for the real neighbourhood.
void DrawShapeGlyph(ImDrawList* draw_list, ImVec2 cursor, TileShape shape, bool is_selected,
                    bool is_hovered) {
  const ImVec2 max = ImVec2(cursor.x + kShapeSize, cursor.y + kShapeSize);
  draw_list->AddRectFilled(cursor, max, IM_COL32(28, 28, 32, 255));

  const absl::Span<const TilePoint> polygon = TileShapePolygon(shape);
  if (!polygon.empty()) {
    std::vector<ImVec2> points;
    points.reserve(polygon.size());
    for (const TilePoint& point : polygon) {
      points.push_back(ImVec2(cursor.x + point.x * kShapeSize, cursor.y + point.y * kShapeSize));
    }
    const ImU32 fill = is_selected ? IM_COL32(120, 170, 255, 255) : IM_COL32(170, 175, 185, 255);
    draw_list->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), fill);
  }

  DrawPaletteItemFrame(*draw_list, cursor, max, is_selected, is_hovered);
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

  DrawPaletteItemFrame(*draw_list, cursor, max, is_selected, is_hovered);
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
      DrawSwatch(draw_list, cursor, atlas, TerrainSwatchTile(*selected_tileset_, terrain),
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

void TerrainPalettePanel::RenderShapePicker() {
  const Terrain* terrain = nullptr;
  if (selected_terrain_id_.has_value()) {
    for (const Terrain& candidate : selected_tileset_->terrains) {
      if (candidate.id == *selected_terrain_id_) terrain = &candidate;
    }
  }

  if (terrain == nullptr) {
    gui_->TextDisabled("Select a terrain to choose a shape.");
    return;
  }

  const std::vector<TerrainShapeChoice> choices =
      ShapeChoicesWithin(PaintableShapesOf(*terrain, *selected_tileset_));
  if (choices.empty()) {
    gui_->TextDisabled("This terrain has artwork for no shape.");
    return;
  }

  // Selecting a different terrain can strand a shape the new one cannot paint,
  // so the picker moves to something it can rather than leaving the brush
  // pointed at geometry with no artwork behind it.
  bool selection_is_offered = false;
  for (const TerrainShapeChoice& choice : choices) {
    if (choice.shape == selected_shape_) selection_is_offered = true;
  }
  if (!selection_is_offered) selected_shape_ = choices.front().shape;

  // Laid out as a grid of silhouettes rather than a list of names. There are
  // twenty-five of these, and a name like "Steep ceiling, down to the left,
  // bottom cell" describes a picture nobody should have to reconstruct. The
  // names stay as tooltips, where they explain a glyph instead of replacing it.
  std::string_view rendered_group;
  for (const TerrainShapeChoice& choice : choices) {
    const bool starts_group = choice.group != rendered_group;
    if (starts_group) {
      gui_->TextDisabled("%s", choice.group.c_str());
      rendered_group = choice.group;
    } else {
      gui_->SameLine();
    }

    ScopedId scoped_id = gui_->CreateScopedId(static_cast<int>(choice.shape));
    const ImVec2 cursor = gui_->GetCursorScreenPos();
    gui_->InvisibleButton("##shape", ImVec2(kShapeSize, kShapeSize));
    const bool clicked = gui_->IsItemClicked(0);
    const bool hovered = gui_->IsItemHovered();
    if (hovered) gui_->SetTooltip("%s", choice.name.c_str());

    if (ImDrawList* draw_list = gui_->GetWindowDrawList(); draw_list != nullptr) {
      DrawShapeGlyph(draw_list, cursor, choice.shape, choice.shape == selected_shape_, hovered);
    }

    if (clicked) selected_shape_ = choice.shape;
  }
}

absl::Status TerrainPalettePanel::Render() {
  ASSIGN_OR_RETURN(
      const TilesetSelectorResult selection,
      tileset_selector_.Render(api_, *gui_, "Tileset##terrain_palette", "terrain_ts_"));
  selected_tileset_ = selection.tileset;
  if (selection.selection_changed) selected_terrain_id_.reset();

  if (selected_tileset_ == nullptr) {
    gui_->TextDisabled(selection.catalog_empty ? "No tilesets loaded." : "Select a tileset above.");
    return absl::OkStatus();
  }

  RenderShapePicker();

  // An unset or unloaded texture is a valid authoring state; swatches fall back
  // to placeholders rather than failing the frame.
  AtlasBinding atlas;
  if (!selected_tileset_->texture_id.empty()) {
    ASSIGN_OR_RETURN(TextureHandle handle, api_.GetTextureHandle(selected_tileset_->texture_id));
    ASSIGN_OR_RETURN(atlas, texture_preview_.BindAtlas(handle));
  }

  return RenderTerrainList(atlas);
}

}  // namespace zebes
