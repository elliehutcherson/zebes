#include "editor/level_editor/level_editor.h"

#include <algorithm>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "common/status_macros.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "editor/level_editor/level_panel.h"
#include "editor/level_editor/level_panel_interface.h"
#include "editor/level_editor/parallax_layout.h"
#include "imgui.h"
#include "parallax_theme_panel.h"

namespace zebes {
namespace {

constexpr ImGuiTableFlags kTableFlags =
    ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoHostExtendY;

constexpr float kPanelGap = 8.0f;
constexpr float kPaletteHeightFraction = 0.25f;
constexpr float kMinimumWorkspaceHeight = 180.0f;
constexpr float kMinimumPaletteHeight = 120.0f;
constexpr float kMaximumPaletteHeight = 260.0f;
constexpr float kConstrainedWorkspaceFraction = 0.6f;

}  // namespace

LevelEditorPanelLayout CalculateLevelEditorPanelLayout(float available_height) {
  const float usable_height = std::max(0.0f, available_height - kPanelGap);
  if (usable_height < kMinimumWorkspaceHeight + kMinimumPaletteHeight) {
    const float workspace_height = usable_height * kConstrainedWorkspaceFraction;
    return {
        .workspace_height = workspace_height,
        .palette_height = usable_height - workspace_height,
    };
  }

  const float palette_height =
      std::clamp(available_height * kPaletteHeightFraction, kMinimumPaletteHeight,
                 kMaximumPaletteHeight);
  return {
      .workspace_height = usable_height - palette_height,
      .palette_height = palette_height,
  };
}

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
    ASSIGN_OR_RETURN(level_panel_, LevelPanel::Create(gui_));
  }
  RefreshLevelCatalog();

  if (options.parallax_theme_panel) {
    parallax_theme_panel_ = std::move(options.parallax_theme_panel);
  } else {
    ASSIGN_OR_RETURN(parallax_theme_panel_, ParallaxThemePanel::Create({.api = api_, .gui = gui_}));
  }

  if (options.parallax_zone_panel) {
    parallax_zone_panel_ = std::move(options.parallax_zone_panel);
  } else {
    ASSIGN_OR_RETURN(parallax_zone_panel_, ParallaxZonePanel::Create({.gui = gui_}));
  }

  if (options.palette_panel) {
    palette_panel_ = std::move(options.palette_panel);
  } else {
    ASSIGN_OR_RETURN(palette_panel_, PalettePanel::Create({.api = api_, .gui = gui_}));
  }

  viewport_tab_ = std::make_unique<ViewportTab>(*api_, gui_, options.terrain_ghost);

  return absl::OkStatus();
}

void LevelEditor::RefreshLevelCatalog() {
  level_model_.SetLevels(api_->GetAllLevels());

  // Offered by the level's Tileset field. Only identity is kept: the panel
  // never reads tile data, and holding whole tilesets would go stale.
  std::vector<TilesetChoice> choices;
  for (const Tileset& tileset : api_->GetAllTilesets()) {
    choices.push_back({.id = tileset.id, .name = tileset.name});
  }
  level_model_.SetTilesetChoices(std::move(choices));
}

absl::Status LevelEditor::SaveActiveLevel() {
  ASSIGN_OR_RETURN(Level level, level_model_.BuildSaveRequest());

  // Artwork first. A derived terrain's tiles are invented while painting, and a
  // level naming tiles that are not on disk is a level that will not open.
  RETURN_IF_ERROR(derived_terrain_.Commit(*api_));

  if (level.id.empty()) {
    ASSIGN_OR_RETURN(std::string id, api_->CreateLevel(std::move(level)));
    RETURN_IF_ERROR(level_model_.FinishCreate(id));
  } else {
    RETURN_IF_ERROR(api_->UpdateLevel(std::move(level)));
    level_model_.MarkSaved();
  }
  RefreshLevelCatalog();
  return absl::OkStatus();
}

absl::Status LevelEditor::HandleLevelPanelEvent(LevelPanelEvent event) {
  switch (event.action) {
    case LevelPanelAction::kNone:
      return absl::OkStatus();
    case LevelPanelAction::kCreate:
      RETURN_IF_ERROR(SaveActiveLevel());
      selection_.Clear();
      selection_.type = SelectionState::Type::kLevel;
      return absl::OkStatus();
    case LevelPanelAction::kOpen:
      viewport_tab_->Reset();
      selection_.Clear();
      selection_.type = SelectionState::Type::kLevel;
      return absl::OkStatus();
    case LevelPanelAction::kSave:
      return SaveActiveLevel();
    case LevelPanelAction::kDelete:
      if (!level_model_.has_level_selection()) {
        return absl::FailedPreconditionError("No level is selected");
      }
      RETURN_IF_ERROR(api_->DeleteLevel(level_model_.selected_level_id()));
      level_model_.FinishDelete();
      viewport_tab_->Reset();
      selection_.Clear();
      RefreshLevelCatalog();
      return absl::OkStatus();
    case LevelPanelAction::kClose:
      viewport_tab_->Reset();
      selection_.Clear();
      save_error_.reset();
      return absl::OkStatus();
  }
  return absl::InternalError("Unknown level panel action");
}

absl::Status LevelEditor::Render() {
  const LevelEditorPanelLayout layout =
      CalculateLevelEditorPanelLayout(gui_->GetContentRegionAvail().y);
  {
    ScopedTable table = gui_->CreateScopedTable(
        "LevelEditorLayout", 3, kTableFlags, ImVec2(0.0f, layout.workspace_height));
    if (!table) return absl::OkStatus();

    // Setup columns with relative sizing
    gui_->TableSetupColumn("Navigator", ImGuiTableColumnFlags_WidthStretch, 0.2f);
    gui_->TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch, 0.6f);
    gui_->TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, 0.2f);

    gui_->TableNextRow();

    gui_->TableNextColumn();
    {
      ScopedChild navigator =
          gui_->CreateScopedChild("LevelEditorNavigator", ImVec2(0.0f, 0.0f));
      if (navigator) RETURN_IF_ERROR(RenderNavigator());
    }

    gui_->TableNextColumn();
    RETURN_IF_ERROR(RenderViewport());

    gui_->TableNextColumn();
    {
      ScopedChild inspector =
          gui_->CreateScopedChild("LevelEditorInspector", ImVec2(0.0f, 0.0f));
      if (inspector) RETURN_IF_ERROR(RenderInspector());
    }
  }

  gui_->Separator();
  ScopedChild palette = gui_->CreateScopedChild(
      "LevelEditorPalette", ImVec2(0.0f, layout.palette_height), true);
  if (!palette) return absl::OkStatus();
  RETURN_IF_ERROR(RenderPalette());

  return absl::OkStatus();
}

absl::Status LevelEditor::RenderPalette() {
  int tile_render_w = 16, tile_render_h = 16;
  if (const Level* level = level_model_.active_level(); level != nullptr) {
    tile_render_w = level->tile_render_width;
    tile_render_h = level->tile_render_height;
  }
  return palette_panel_->Render(tile_render_w, tile_render_h);
}

absl::Status LevelEditor::RenderNavigator() {
  // If no level is loaded, show the Project Browser (List of Levels)
  if (!level_model_.has_active_level()) {
    selection_.Clear();
    ASSIGN_OR_RETURN(LevelPanelEvent event, level_panel_->RenderList(level_model_));
    return HandleLevelPanelEvent(event);
  }

  // A Level is loaded. Render the Scene Graph.
  Level& level = *level_model_.active_level();

  if (gui_->Button("Close Level")) {
    level_model_.CloseActiveLevel();
    viewport_tab_->Reset();
    selection_.Clear();
    save_error_.reset();
    return absl::OkStatus();
  }
  gui_->SameLine();
  if (gui_->Button("Save Level")) {
    absl::Status status = SaveActiveLevel();
    if (!status.ok()) {
      save_error_ = status.message();
    } else {
      save_error_.reset();
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

  const std::string level_label = absl::StrCat(
      level.name.empty() ? "(unnamed level)" : level.name, "##level_", level.id);
  bool root_open = gui_->CollapsingHeader(level_label.c_str(), root_flags);
  if (gui_->IsItemClicked()) {
    selection_.Clear();
    selection_.type = SelectionState::Type::kLevel;
  }

  if (root_open) {
    // 1. Parallax
    if (gui_->CollapsingHeader("Parallax", ImGuiTreeNodeFlags_DefaultOpen)) {
      RETURN_IF_ERROR(parallax_theme_panel_->RenderNavigator(level, selection_));

      std::optional<int> previous_zone_id;
      if (selection_.type == SelectionState::Type::kZone) {
        previous_zone_id = selection_.zone_id;
      }
      RETURN_IF_ERROR(parallax_zone_panel_->RenderNavigator(level, selection_));

      if (selection_.type == SelectionState::Type::kZone &&
          previous_zone_id != selection_.zone_id) {
        if (const ParallaxZone* zone = FindParallaxZoneById(level.zones, selection_.zone_id);
            zone != nullptr) {
          viewport_tab_->FrameZone(*zone);
        }
      }
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

  if (!level_model_.has_active_level()) {
    gui_->TextDisabled("No Level Loaded");
    return absl::OkStatus();
  }

  switch (selection_.type) {
    case SelectionState::Type::kNone:
      gui_->TextDisabled("Select an item to view properties.");
      break;

    case SelectionState::Type::kLevel:
      {
        ASSIGN_OR_RETURN(LevelPanelEvent event, level_panel_->RenderDetails(level_model_));
        RETURN_IF_ERROR(HandleLevelPanelEvent(event));
      }
      break;

    case SelectionState::Type::kTheme:
      RETURN_IF_ERROR(
          parallax_theme_panel_->RenderThemeDetails(*level_model_.active_level(), selection_));
      break;

    case SelectionState::Type::kLayer:
      RETURN_IF_ERROR(
          parallax_theme_panel_->RenderLayerDetails(*level_model_.active_level(), selection_));
      break;

    case SelectionState::Type::kZone:
      if (gui_->Button("Frame Zone")) {
        const Level& level = *level_model_.active_level();
        if (const ParallaxZone* zone =
                FindParallaxZoneById(level.zones, selection_.zone_id);
            zone != nullptr) {
          viewport_tab_->FrameZone(*zone);
        }
      }
      RETURN_IF_ERROR(
          parallax_zone_panel_->RenderDetails(*level_model_.active_level(), selection_));
      break;

    case SelectionState::Type::kEntity: {
      Level& level = *level_model_.active_level();
      auto it = level.entities.find(selection_.entity_id);
      if (it == level.entities.end()) {
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

      // Higher draws later, so it appears in front of entities with a lower
      // value. It orders entities against each other only: the draw passes are
      // fixed, so no value here puts an entity behind the terrain.
      int sort_order = entity.sort_order;
      if (gui_->InputInt("Draw Order", &sort_order)) {
        entity.sort_order = sort_order;
      }
      gui_->Separator();

      if (gui_->Button("Remove Entity")) {
        level.entities.erase(it);
        selection_.Clear();
      }
      break;
    }
  }
  return absl::OkStatus();
}

void LevelEditor::RenderTilesetMismatchWarning(const Level& level,
                                               const Tileset* rejected_tileset) {
  if (rejected_tileset == nullptr) return;

  absl::StatusOr<Tileset*> level_tileset = api_->GetTileset(level.tileset_id);
  const char* level_name = (level_tileset.ok() && *level_tileset != nullptr)
                               ? (*level_tileset)->name.c_str()
                               : level.tileset_id.c_str();
  gui_->TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                    "Painting is off: the palette shows '%s' but this level uses '%s'. Select "
                    "'%s' in the palette, or change the level's Tileset in its details panel.",
                    rejected_tileset->name.c_str(), level_name, level_name);
}

absl::Status LevelEditor::RenderViewport() {
  gui_->Text("Viewport");
  gui_->Separator();

  ScopedChild viewport = gui_->CreateScopedChild("LevelViewport", ImVec2(0, 0), true);
  if (!viewport) return absl::OkStatus();

  Level* level = level_model_.active_level();
  if (level == nullptr) {
    gui_->TextDisabled("No level selected.");
    return absl::OkStatus();
  }

  const Tileset* terrain_tileset = palette_panel_->GetSelectedTerrainTileset();
  const PaletteSelection selection{
      .tile_tileset = palette_panel_->GetSelectedTileset(),
      .tile = palette_panel_->GetSelectedTile(),
      .terrain_tileset = terrain_tileset,
      .terrain_id = palette_panel_->GetSelectedTerrainId(),
  };
  const PaletteBinding binding = ResolvePaletteBinding(*level, selection);
  level->tileset_id = binding.tileset_id;
  RenderTilesetMismatchWarning(*level, binding.rejected_tileset);

  // The tileset the level is bound to, from Api storage: the same object the
  // viewport resolves for rendering. A derived terrain grows it, so the
  // session, the index and the viewport must all read one tileset or they
  // would disagree about which tile IDs exist.
  Tileset* bound_tileset = nullptr;
  if (!level->tileset_id.empty()) {
    ASSIGN_OR_RETURN(bound_tileset, api_->GetTileset(level->tileset_id));
  }

  // Opened before the index is built, because a derived terrain grows the
  // tileset and the index has to see every tile that exists.
  if (bound_tileset != nullptr) {
    RETURN_IF_ERROR(derived_terrain_.OpenFor(*api_, *bound_tileset));
  }

  // Rebuilt every frame because the tileset's terrains can change in the
  // Tileset Editor between frames, and because painting a derived terrain adds
  // tiles the next frame's neighbours must be able to recognise.
  std::optional<TerrainIndex> terrain_index;
  std::optional<int> paint_terrain_id = binding.terrain_id;
  if (paint_terrain_id.has_value() && bound_tileset != nullptr) {
    ASSIGN_OR_RETURN(terrain_index, TerrainIndex::Build(*bound_tileset));
  }
  if (!terrain_index.has_value()) paint_terrain_id.reset();

  // Which provider answers is the scheme's whole difference. The authored one
  // is rebuilt per frame because it holds nothing; the derived one is the
  // session's, because it holds artwork.
  std::optional<Blob47TileProvider> authored_provider;
  TerrainTileProvider* terrain_provider = derived_terrain_.provider();
  if (terrain_provider == nullptr && terrain_index.has_value()) {
    authored_provider.emplace(*terrain_index);
    terrain_provider = &*authored_provider;
  }

  const absl::Status rendered = viewport_tab_->Render({
      .level = level,
      .paint_terrain_id = paint_terrain_id,
      .paint_shape = palette_panel_->GetSelectedTerrainShape(),
      .terrain_index = terrain_index.has_value() ? &*terrain_index : nullptr,
      .terrain_provider = terrain_provider,
      .placement_blueprint = palette_panel_->GetSelectedBlueprint(),
      .selected_entity_id = (selection_.type == SelectionState::Type::kEntity)
                                ? selection_.entity_id
                                : Entity::kInvalidId,
      .snap_to_grid = palette_panel_->GetSnapToGrid(),
      .show_entity_borders = palette_panel_->GetShowEntityBorders(),
      .delete_mode = palette_panel_->GetDeleteMode(),
      .placement_tile = binding.tile,
      .show_tile_frame = palette_panel_->GetShowTileFrame(),
      .show_tile_collision = palette_panel_->GetShowTileCollision(),
      .tile_overlay_opacity = palette_panel_->GetTileOverlayOpacity(),
      .entity_overlay_opacity = palette_panel_->GetEntityOverlayOpacity(),
      .selected_zone_id = (selection_.type == SelectionState::Type::kZone)
                              ? std::optional<int>(selection_.zone_id)
                              : std::nullopt,
      .selected_parallax_theme_id = (selection_.type == SelectionState::Type::kTheme ||
                                     selection_.type == SelectionState::Type::kLayer)
                                        ? std::optional<int>(selection_.theme_id)
                                        : std::nullopt,
      .selected_parallax_layer_index = (selection_.type == SelectionState::Type::kLayer)
                                           ? std::optional<int>(selection_.layer_index)
                                           : std::nullopt,
  });

  // Painting may have rendered artwork the atlas did not hold. Upload it before
  // the frame ends, or the cells that reference it draw as holes.
  //
  // Before reporting a failed render, deliberately. A draw that failed because
  // the atlas outgrew its uploaded texture must not also skip the upload that
  // would fix it: doing so made the error permanent, since every later frame
  // failed the same way and skipped the same upload.
  RETURN_IF_ERROR(derived_terrain_.ShowNewArtwork(*api_));
  RETURN_IF_ERROR(rendered);

  std::optional<uint64_t> delete_request = viewport_tab_->TakeDeleteRequest();
  if (delete_request.has_value()) {
    if (selection_.entity_id == *delete_request) selection_.Clear();
    level->entities.erase(*delete_request);
  }

  std::optional<Entity> new_entity = viewport_tab_->TakeNewEntity();
  if (new_entity.has_value()) {
    level->entities[new_entity->id] = std::move(*new_entity);
  }

  std::optional<uint64_t> click_selection = viewport_tab_->TakeClickSelection();
  if (click_selection.has_value()) {
    selection_.ApplyEntityPick(*click_selection);
  }

  return absl::OkStatus();
}

}  // namespace zebes
