#include "editor/level_editor/blueprint_palette_panel.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "editor/palette_ui.h"
#include "editor/texture_preview.h"
#include "imgui.h"
#include "objects/sprite.h"

namespace zebes {
namespace {

constexpr float kCardWidth = 84.0f;
constexpr float kCardHeight = 92.0f;
constexpr float kCardPad = 8.0f;
constexpr float kImageInset = 6.0f;
constexpr float kImageSize = 64.0f;
constexpr float kLabelTop = 73.0f;

struct BlueprintThumbnail {
  SpriteFrame frame;
  AtlasBinding atlas;

  bool IsDrawable() const {
    const int64_t texture_right = static_cast<int64_t>(frame.texture_x) + frame.texture_w;
    const int64_t texture_bottom = static_cast<int64_t>(frame.texture_y) + frame.texture_h;
    return atlas.IsValid() && frame.texture_w > 0 && frame.texture_h > 0 && frame.render_w > 0 &&
           frame.render_h > 0 && frame.texture_x >= 0 && frame.texture_y >= 0 &&
           texture_right <= atlas.width && texture_bottom <= atlas.height;
  }
};

void DrawBlueprintCard(ImDrawList& draw_list, ImVec2 card_min,
                       const BlueprintPaletteEntry& blueprint,
                       const std::optional<BlueprintThumbnail>& thumbnail, bool selected,
                       bool hovered, float overlay_opacity) {
  const ImVec2 card_max(card_min.x + kCardWidth, card_min.y + kCardHeight);
  draw_list.AddRectFilled(card_min, card_max,
                          selected ? IM_COL32(48, 80, 132, 255) : IM_COL32(52, 52, 52, 255));

  const ImVec2 image_area_min(card_min.x + kImageInset, card_min.y + kImageInset);
  const ImVec2 image_area_max(image_area_min.x + kImageSize, image_area_min.y + kImageSize);
  draw_list.AddRectFilled(image_area_min, image_area_max, IM_COL32(38, 38, 38, 255));

  if (thumbnail.has_value() && thumbnail->IsDrawable()) {
    const SpriteFrame& frame = thumbnail->frame;
    const AtlasBinding& atlas = thumbnail->atlas;
    const float scale = std::min(kImageSize / frame.render_w, kImageSize / frame.render_h);
    const float image_width = frame.render_w * scale;
    const float image_height = frame.render_h * scale;
    const ImVec2 image_min(image_area_min.x + (kImageSize - image_width) / 2.0f,
                           image_area_min.y + (kImageSize - image_height) / 2.0f);
    const ImVec2 image_max(image_min.x + image_width, image_min.y + image_height);
    const ImVec2 uv_min(static_cast<float>(frame.texture_x) / atlas.width,
                        static_cast<float>(frame.texture_y) / atlas.height);
    const ImVec2 uv_max(
        static_cast<float>(static_cast<int64_t>(frame.texture_x) + frame.texture_w) / atlas.width,
        static_cast<float>(static_cast<int64_t>(frame.texture_y) + frame.texture_h) / atlas.height);
    draw_list.AddImage(atlas.texture_id, image_min, image_max, uv_min, uv_max);
  } else {
    draw_list.AddLine(image_area_min, image_area_max, IM_COL32(90, 90, 90, 255));
    draw_list.AddLine(ImVec2(image_area_max.x, image_area_min.y),
                      ImVec2(image_area_min.x, image_area_max.y), IM_COL32(90, 90, 90, 255));
  }

  if (overlay_opacity > 0.0f) {
    draw_list.AddRectFilled(image_area_min, image_area_max,
                            IM_COL32(255, 204, 0, static_cast<uint8_t>(overlay_opacity * 255.0f)));
  }

  draw_list.PushClipRect(ImVec2(card_min.x + 4.0f, card_min.y + kLabelTop),
                         ImVec2(card_max.x - 4.0f, card_max.y - 3.0f), true);
  draw_list.AddText(ImVec2(card_min.x + 4.0f, card_min.y + kLabelTop), IM_COL32(230, 230, 230, 255),
                    blueprint.name.empty() ? "(unnamed)" : blueprint.name.c_str());
  draw_list.PopClipRect();
  DrawPaletteItemFrame(draw_list, card_min, card_max, selected, hovered);
}

}  // namespace

absl::StatusOr<std::unique_ptr<BlueprintPalettePanel>> BlueprintPalettePanel::Create(
    Options options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null.");
  }
  if (options.gui == nullptr) {
    return absl::InvalidArgumentError("Gui must not be null.");
  }
  return absl::WrapUnique(new BlueprintPalettePanel(std::move(options)));
}

BlueprintPalettePanel::BlueprintPalettePanel(Options options)
    : api_(*options.api), gui_(options.gui) {}

const Blueprint* BlueprintPalettePanel::GetSelectedBlueprint() const {
  if (!model_.selected_blueprint_id().has_value()) return nullptr;
  absl::StatusOr<Blueprint*> blueprint = api_.GetBlueprint(*model_.selected_blueprint_id());
  return blueprint.ok() ? *blueprint : nullptr;
}

absl::Status BlueprintPalettePanel::Render() {
  auto child = ScopedChild(gui_, "BlueprintPalette", ImVec2(0, 0), true);
  if (!child) return absl::OkStatus();

  const std::vector<Blueprint> blueprints = api_.GetAllBlueprints();
  RETURN_IF_ERROR(model_.SetBlueprints(blueprints));

  gui_->SetNextItemWidth(240.0f);
  std::string search_query = model_.search_query();
  if (gui_->InputText("Search##blueprint_palette", &search_query)) {
    model_.SetSearchQuery(std::move(search_query));
  }
  gui_->SameLine();
  gui_->Checkbox("Snap to Grid", &snap_to_grid_);
  gui_->SameLine();
  gui_->Checkbox("Show Entity Borders", &show_entity_borders_);
  gui_->SameLine();
  gui_->SliderFloat("Entity Overlay", &entity_overlay_opacity_, /*v_min=*/0.0f, /*v_max=*/1.0f);

  auto grid = ScopedChild(gui_, "BlueprintGrid", ImVec2(0, 0), false);
  if (!grid) return absl::OkStatus();

  ASSIGN_OR_RETURN(
      const PaletteGridLayout layout,
      CalculatePaletteGridLayout(gui_->GetContentRegionAvail().x, kCardWidth, kCardPad));
  std::map<std::string, AtlasBinding> atlas_cache;
  int rendered_count = 0;

  for (const BlueprintPaletteEntry* blueprint : model_.FilteredEntries()) {
    const bool is_selected = model_.selected_blueprint_id().has_value() &&
                             *model_.selected_blueprint_id() == blueprint->id;
    std::optional<BlueprintThumbnail> thumbnail;
    if (blueprint->preview.sprite_id.has_value()) {
      absl::StatusOr<Sprite*> sprite = api_.GetSprite(*blueprint->preview.sprite_id);
      if (sprite.ok() && *sprite != nullptr && !(*sprite)->frames.empty() &&
          !(*sprite)->texture_id.empty()) {
        const std::string& texture_id = (*sprite)->texture_id;
        auto atlas = atlas_cache.find(texture_id);
        if (atlas == atlas_cache.end()) {
          AtlasBinding binding;
          absl::StatusOr<TextureHandle> handle = api_.GetTextureHandle(texture_id);
          if (handle.ok()) {
            absl::StatusOr<AtlasBinding> resolved = TexturePreviewRenderer::BindAtlas(*handle);
            if (resolved.ok()) binding = *resolved;
          }
          atlas = atlas_cache.emplace(texture_id, binding).first;
        }
        thumbnail = BlueprintThumbnail{
            .frame = (*sprite)->frames.front(),
            .atlas = atlas->second,
        };
      }
    }

    ScopedId id = gui_->CreateScopedId(blueprint->id.c_str());
    const ImVec2 cursor = gui_->GetCursorScreenPos();
    gui_->InvisibleButton("##blueprint", ImVec2(kCardWidth, kCardHeight));
    const bool clicked = gui_->IsItemClicked(0);
    const bool hovered = gui_->IsItemHovered();
    if (hovered) {
      gui_->SetTooltip(
          "%s%s", blueprint->name.empty() ? "(unnamed blueprint)" : blueprint->name.c_str(),
          thumbnail.has_value() && thumbnail->IsDrawable() ? "" : "\nNo artwork preview");
    }

    if (ImDrawList* draw_list = gui_->GetWindowDrawList(); draw_list != nullptr) {
      DrawBlueprintCard(*draw_list, cursor, *blueprint, thumbnail, is_selected, hovered,
                        entity_overlay_opacity_);
    }

    if (clicked) RETURN_IF_ERROR(model_.ToggleSelection(blueprint->id));

    ++rendered_count;
    if (layout.ContinueRowAfter(rendered_count)) gui_->SameLine();
  }

  if (rendered_count == 0) {
    gui_->TextDisabled(blueprints.empty() ? "No blueprints loaded."
                                          : "No blueprints match the search.");
  }

  return absl::OkStatus();
}

}  // namespace zebes
