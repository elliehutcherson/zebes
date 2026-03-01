#include "editor/level_editor/level_editor.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "common/status_macros.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "editor/level_editor/level_panel.h"
#include "editor/level_editor/level_panel_interface.h"
#include "editor/level_editor/parallax_preview_tab.h"
#include "imgui.h"
#include "parallax_theme_panel.h"

namespace zebes {
namespace {

constexpr ImGuiTableFlags kTableFlags =
    ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;

}  // namespace

absl::StatusOr<std::unique_ptr<LevelEditor>> LevelEditor::Create(Options options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null");
  }
  if (options.gui == nullptr) {
    return absl::InvalidArgumentError("Gui must not be null");
  }
  auto editor = absl::WrapUnique(new LevelEditor(options.api, options.gui));
  RETURN_IF_ERROR(editor->Init(std::move(options)));
  return editor;
}

LevelEditor::LevelEditor(Api* api, GuiInterface* gui) : api_(api), gui_(gui) {}

absl::Status LevelEditor::Init(Options options) {
  if (options.level_panel) {
    level_panel_ = std::move(options.level_panel);
  } else {
    ASSIGN_OR_RETURN(level_panel_, LevelPanel::Create({
                                       .api = api_,
                                       .gui = gui_,
                                   }));
  }

  if (options.parallax_theme_panel) {
    parallax_theme_panel_ = std::move(options.parallax_theme_panel);
  } else {
    ASSIGN_OR_RETURN(parallax_theme_panel_, ParallaxThemePanel::Create({.api = api_, .gui = gui_}));
  }

  if (options.parallax_zone_panel) {
    parallax_zone_panel_ = std::move(options.parallax_zone_panel);
  } else {
    ASSIGN_OR_RETURN(parallax_zone_panel_, ParallaxZonePanel::Create({.api = api_, .gui = gui_}));
  }

  if (options.palette_panel) {
    palette_panel_ = std::move(options.palette_panel);
  } else {
    ASSIGN_OR_RETURN(palette_panel_, PalettePanel::Create({.api = api_, .gui = gui_}));
  }

  parallax_tab_ = std::make_unique<ParallaxPreviewTab>(*api_, gui_);
  viewport_tab_ = std::make_unique<ViewportTab>(*api_, gui_);

  return absl::OkStatus();
}

absl::Status LevelEditor::Render() {
  {
    ScopedTable table = gui_->CreateScopedTable("LevelEditorLayout", 3, kTableFlags);
    if (!table) return absl::OkStatus();

    // Setup columns with relative sizing
    gui_->TableSetupColumn("Navigator", ImGuiTableColumnFlags_WidthStretch, 0.2f);
    gui_->TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch, 0.6f);
    gui_->TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, 0.2f);

    gui_->TableNextRow();

    gui_->TableNextColumn();
    RETURN_IF_ERROR(RenderNavigator());

    gui_->TableNextColumn();
    RETURN_IF_ERROR(RenderViewport());

    gui_->TableNextColumn();
    RETURN_IF_ERROR(RenderInspector());
  }

  // Render your full-width bottom row outside the table
  gui_->Separator();
  RETURN_IF_ERROR(RenderPalette());

  return absl::OkStatus();
}

absl::Status LevelEditor::RenderPalette() {
  int tile_render_w = 16, tile_render_h = 16;
  if (editting_level_.has_value()) {
    tile_render_w = editting_level_->tile_render_width;
    tile_render_h = editting_level_->tile_render_height;
  }
  return palette_panel_->Render(tile_render_w, tile_render_h);
}

absl::Status LevelEditor::RenderNavigator() {
  // If no level is loaded, show the Project Browser (List of Levels)
  if (!editting_level_.has_value()) {
    selection_.Clear();
    // Use the LevelPanel's list view to select a level to edit.
    RETURN_IF_ERROR(level_panel_->RenderList(editting_level_, selection_));
    return absl::OkStatus();
  }

  // A Level is loaded. Render the Scene Graph.
  Level& level = *editting_level_;

  if (gui_->Button("Close Level")) {
    editting_level_.reset();
    selection_.Clear();
    save_error_.reset();
    return absl::OkStatus();
  }
  gui_->SameLine();
  if (gui_->Button("Save Level")) {
    absl::Status status = api_->UpdateLevel(*editting_level_);
    if (!status.ok()) {
      save_error_ = status.message();
    } else {
      save_error_.reset();
      level_panel_->RefreshCache();
    }
  }

  if (save_error_.has_value()) {
    gui_->TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Save failed: %s", save_error_->c_str());
  }
  gui_->Separator();

  // Root Node: The Level itself
  ImGuiTreeNodeFlags root_flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
  if (selection_.type == SelectionState::Type::kLevel) {
    root_flags |= ImGuiTreeNodeFlags_Selected;
  }

  bool root_open = gui_->CollapsingHeader(level.name.c_str(), root_flags);
  if (gui_->IsItemClicked()) {
    selection_.Clear();
    selection_.type = SelectionState::Type::kLevel;
  }

  if (root_open) {
    // 1. Parallax
    if (gui_->CollapsingHeader("Parallax", ImGuiTreeNodeFlags_DefaultOpen)) {
      RETURN_IF_ERROR(parallax_theme_panel_->RenderNavigator(level, selection_));
      RETURN_IF_ERROR(parallax_zone_panel_->RenderNavigator(level, selection_));
    }

    // 2. Entities
    if (gui_->CollapsingHeader("Entities", ImGuiTreeNodeFlags_DefaultOpen)) {
      // Entity list with right-click context menu for deletion.
      std::optional<uint64_t> entity_to_delete;
      for (const auto& [id, entity] : level.entities) {
        ScopedId scoped_id = gui_->CreateScopedId(static_cast<const void*>(&entity));

        std::string label = entity.blueprint_id.empty()
                                ? absl::StrFormat("Entity %llu", entity.id)
                                : absl::StrFormat("%s (%llu)", entity.blueprint_id, entity.id);

        bool is_selected =
            (selection_.type == SelectionState::Type::kEntity && selection_.entity_id == id);
        gui_->Selectable(label.c_str(), is_selected);
        if (gui_->IsItemClicked()) {
          selection_.type = SelectionState::Type::kEntity;
          selection_.entity_id = id;
        }

        if (auto popup = gui_->CreateScopedPopupContextItem(); popup) {
          if (gui_->MenuItem("Delete Entity")) {
            entity_to_delete = id;
          }
        }
      }

      // Erase after the loop to avoid invalidating iterators mid-iteration.
      if (entity_to_delete.has_value()) {
        if (selection_.entity_id == *entity_to_delete) selection_.Clear();
        level.entities.erase(*entity_to_delete);
      }
    }
  }

  return absl::OkStatus();
}

absl::Status LevelEditor::RenderInspector() {
  gui_->Text("Inspector");
  gui_->Separator();

  if (!editting_level_.has_value()) {
    gui_->TextDisabled("No Level Loaded");
    return absl::OkStatus();
  }

  switch (selection_.type) {
    case SelectionState::Type::kNone:
      gui_->TextDisabled("Select an item to view properties.");
      break;

    case SelectionState::Type::kLevel:
      RETURN_IF_ERROR(level_panel_->RenderDetails(editting_level_, selection_));
      break;

    case SelectionState::Type::kTheme:
      RETURN_IF_ERROR(parallax_theme_panel_->RenderThemeDetails(*editting_level_, selection_));
      break;

    case SelectionState::Type::kLayer:
      RETURN_IF_ERROR(parallax_theme_panel_->RenderLayerDetails(*editting_level_, selection_));
      break;

    case SelectionState::Type::kZone:
      RETURN_IF_ERROR(parallax_zone_panel_->RenderDetails(*editting_level_, selection_));
      break;

    case SelectionState::Type::kEntity: {
      auto it = editting_level_->entities.find(selection_.entity_id);
      if (it == editting_level_->entities.end()) {
        selection_.Clear();
        break;
      }
      Entity& entity = it->second;

      gui_->Text("ID: %llu", static_cast<unsigned long long>(entity.id));
      if (!entity.blueprint_id.empty()) {
        gui_->Text("Blueprint: %s", entity.blueprint_id.c_str());
      }
      gui_->Separator();

      float pos_x = static_cast<float>(entity.transform.position.x);
      float pos_y = static_cast<float>(entity.transform.position.y);
      if (gui_->InputFloat("X", &pos_x)) {
        entity.transform.position.x = pos_x;
      }
      if (gui_->InputFloat("Y", &pos_y)) {
        entity.transform.position.y = pos_y;
      }
      gui_->Separator();

      if (gui_->Button("Remove Entity")) {
        editting_level_->entities.erase(it);
        selection_.Clear();
      }
      break;
    }
  }
  return absl::OkStatus();
}

absl::Status LevelEditor::RenderViewport() {
  gui_->Text("Viewport");
  gui_->Separator();

  auto tab_default = [this]() -> absl::Status {
    auto tab_item = ScopedTabItem(gui_, "Viewport");
    if (!tab_item) return absl::OkStatus();

    if (!editting_level_.has_value()) {
      gui_->TextDisabled("No level selected.");
      return absl::OkStatus();
    }

    // Auto-associate the level with the selected tileset on first tile selection.
    const Tileset* selected_tileset = palette_panel_->GetSelectedTileset();
    if (selected_tileset != nullptr && editting_level_->tileset_id.empty()) {
      editting_level_->tileset_id = selected_tileset->id;
    }

    RETURN_IF_ERROR(viewport_tab_->Render({
        .level = &*editting_level_,
        .placement_blueprint = palette_panel_->GetSelectedBlueprint(),
        .selected_entity_id = (selection_.type == SelectionState::Type::kEntity)
                                  ? selection_.entity_id
                                  : Entity::kInvalidId,
        .snap_to_grid = palette_panel_->GetSnapToGrid(),
        .show_entity_borders = palette_panel_->GetShowEntityBorders(),
        .delete_mode = palette_panel_->GetDeleteMode(),
        .placement_tile = palette_panel_->GetSelectedTile(),
        .placement_tileset = selected_tileset,
        .show_tile_frame = palette_panel_->GetShowTileFrame(),
        .show_tile_collision = palette_panel_->GetShowTileCollision(),
        .tile_overlay_opacity = palette_panel_->GetTileOverlayOpacity(),
        .entity_overlay_opacity = palette_panel_->GetEntityOverlayOpacity(),
    }));

    // Consume a canvas right-click delete request and remove the entity.
    std::optional<uint64_t> delete_request = viewport_tab_->TakeDeleteRequest();
    if (delete_request.has_value()) {
      if (selection_.entity_id == *delete_request) selection_.Clear();
      editting_level_->entities.erase(*delete_request);
    }

    // Consume a newly placed entity and add it to the level.
    std::optional<Entity> new_entity = viewport_tab_->TakeNewEntity();
    if (new_entity.has_value()) {
      editting_level_->entities[new_entity->id] = std::move(*new_entity);
    }

    // Consume a canvas click and update the editor selection state.
    std::optional<uint64_t> click_sel = viewport_tab_->TakeClickSelection();
    if (click_sel.has_value()) {
      selection_.Clear();
      if (*click_sel != Entity::kInvalidId) {
        selection_.type = SelectionState::Type::kEntity;
        selection_.entity_id = *click_sel;
      }
    }
    return absl::OkStatus();
  };

  auto tab_parallax = [this]() -> absl::Status {
    std::optional<std::string> texture_id =
        parallax_theme_panel_->GetTexture(selection_, *editting_level_);

    if (!texture_id.has_value()) return absl::OkStatus();

    return parallax_tab_->Render(*texture_id);
  };

  // Using a child window for the viewport to clip content if needed and handle scrolling.
  if (auto view_child = ScopedChild(gui_, "LevelViewport", ImVec2(0, 0), true); view_child) {
    if (auto view_tab = ScopedTabBar(gui_, "ViewportTabs"); view_tab) {
      RETURN_IF_ERROR(tab_default());
      RETURN_IF_ERROR(tab_parallax());
    }
  }

  return absl::OkStatus();
}

}  // namespace zebes
