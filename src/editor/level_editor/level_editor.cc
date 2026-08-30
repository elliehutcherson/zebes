#include "editor/level_editor/level_editor.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "common/status_macros.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "editor/inspector_ui.h"
#include "editor/level_editor/level_panel.h"
#include "editor/level_editor/level_panel_interface.h"
#include "editor/level_editor/viewport_model.h"
#include "engine/parallax_layout.h"
#include "imgui.h"

namespace zebes {
namespace {

constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
                                        ImGuiTableFlags_SizingStretchProp |
                                        ImGuiTableFlags_NoHostExtendY;

constexpr float kPanelGap = 8.0f;
constexpr float kPaletteHeightFraction = 0.25f;
constexpr float kMinimumWorkspaceHeight = 180.0f;
constexpr float kMinimumPaletteHeight = 120.0f;
constexpr float kMaximumPaletteHeight = 260.0f;
constexpr float kMaximumUserPaletteHeight = 420.0f;
constexpr float kConstrainedWorkspaceFraction = 0.6f;

}  // namespace

LevelEditorPanelLayout CalculateLevelEditorPanelLayout(float available_height, bool show_palette,
                                                       float preferred_palette_height) {
  if (!show_palette) {
    return {.workspace_height = std::max(0.0f, available_height), .palette_height = 0.0f};
  }
  const float usable_height = std::max(0.0f, available_height - kPanelGap);
  if (usable_height < kMinimumWorkspaceHeight + kMinimumPaletteHeight) {
    const float workspace_height = usable_height * kConstrainedWorkspaceFraction;
    return {
        .workspace_height = workspace_height,
        .palette_height = usable_height - workspace_height,
    };
  }

  const float automatic_height = std::clamp(available_height * kPaletteHeightFraction,
                                            kMinimumPaletteHeight, kMaximumPaletteHeight);
  const float requested_height =
      preferred_palette_height > 0.0f
          ? std::clamp(preferred_palette_height, kMinimumPaletteHeight, kMaximumUserPaletteHeight)
          : automatic_height;
  const float maximum_height = usable_height - kMinimumWorkspaceHeight;
  const float palette_height = std::min(requested_height, maximum_height);
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

  if (options.world_layer_panel) {
    world_layer_panel_ = std::move(options.world_layer_panel);
  } else {
    ASSIGN_OR_RETURN(world_layer_panel_, WorldLayerPanel::Create(gui_));
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
    case LevelPanelAction::kNew:
      world_layer_model_.Open(*level_model_.active_level());
      selection_.Clear();
      selection_.type = SelectionState::Type::kLevel;
      viewport_tab_->Reset();
      return absl::OkStatus();
    case LevelPanelAction::kCreate:
      RETURN_IF_ERROR(SaveActiveLevel());
      world_layer_model_.Open(*level_model_.active_level());
      selection_.Clear();
      selection_.type = SelectionState::Type::kLevel;
      return absl::OkStatus();
    case LevelPanelAction::kOpen:
      viewport_tab_->Reset();
      world_layer_model_.Open(*level_model_.active_level());
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
      world_layer_model_.Close();
      viewport_tab_->Reset();
      selection_.Clear();
      RefreshLevelCatalog();
      return absl::OkStatus();
    case LevelPanelAction::kClose:
      level_model_.CloseActiveLevel();
      viewport_tab_->Reset();
      world_layer_model_.Close();
      selection_.Clear();
      save_error_.reset();
      return absl::OkStatus();
    case LevelPanelAction::kReviewIssues:
      selection_.Clear();
      selection_.type = SelectionState::Type::kLevel;
      return absl::OkStatus();
    case LevelPanelAction::kFrameWorld:
      if (level_model_.active_level() == nullptr) {
        return absl::FailedPreconditionError("No level is open to frame.");
      }
      viewport_tab_->FrameLevel(*level_model_.active_level());
      return absl::OkStatus();
  }
  return absl::InternalError("Unknown level panel action");
}

LevelAuthoringReadiness LevelEditor::CurrentReadiness() const {
  const Level* level = level_model_.active_level();
  if (level == nullptr) return {};

  const std::vector<Tileset> tilesets = api_->GetAllTilesets();
  const bool tileset_resolves =
      !level->tileset_id.empty() &&
      std::any_of(tilesets.begin(), tilesets.end(),
                  [&](const Tileset& tileset) { return tileset.id == level->tileset_id; });
  const bool active_layer_available = world_layer_model_.active_layer(*level) != nullptr;
  const std::vector<ParallaxTheme> themes = api_->GetAllParallaxThemes();
  const bool parallax_theme_available = !themes.empty();
  const bool zone_theme_references_resolve =
      std::all_of(level->zones.begin(), level->zones.end(), [&](const ParallaxZone& zone) {
        return std::any_of(themes.begin(), themes.end(),
                           [&](const ParallaxTheme& theme) { return theme.id == zone.theme_id; });
      });
  return EvaluateLevelAuthoringReadiness(*level, tileset_resolves, active_layer_available,
                                         parallax_theme_available, zone_theme_references_resolve);
}

absl::Status LevelEditor::RenderToolbar() {
  const LevelAuthoringReadiness readiness = CurrentReadiness();
  ASSIGN_OR_RETURN(const LevelPanelEvent event,
                   level_panel_->RenderToolbar(level_model_, readiness));
  const absl::Status event_status = HandleLevelPanelEvent(event);
  if (!event_status.ok()) {
    save_error_ = event_status.message();
  } else if (event.action == LevelPanelAction::kCreate || event.action == LevelPanelAction::kSave) {
    save_error_.reset();
  }

  const Level* level = level_model_.active_level();
  if (level == nullptr) return absl::OkStatus();
  gui_->SameLine();
  const char* level_name = level->name.empty() ? "(unnamed level)" : level->name.c_str();
  if (level_model_.has_unsaved_changes()) {
    gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "%s *", level_name);
    if (gui_->IsItemHovered()) gui_->SetTooltip("Unsaved level changes");
  } else {
    gui_->Text("%s", level_name);
  }
  if (readiness.can_place()) {
    gui_->SameLine();
    if (gui_->Button(show_palette_ ? "Hide Palette" : "Show Palette")) {
      show_palette_ = !show_palette_;
    }
  }
  if (save_error_) {
    gui_->SameLine();
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Save failed: %s", save_error_->c_str());
  }
  return absl::OkStatus();
}

absl::Status LevelEditor::Render() {
  if (level_model_.has_active_level()) {
    RETURN_IF_ERROR(RenderToolbar());
    if (!level_model_.has_active_level()) return absl::OkStatus();
    gui_->Separator();
  }
  const bool render_palette = show_palette_ && CurrentReadiness().can_place();
  const LevelEditorPanelLayout layout = CalculateLevelEditorPanelLayout(
      gui_->GetContentRegionAvail().y, render_palette, palette_height_);
  {
    ScopedTable table = gui_->CreateScopedTable("LevelEditorLayout", 3, kTableFlags,
                                                ImVec2(0.0f, layout.workspace_height));
    if (!table) return absl::OkStatus();

    gui_->TableSetupColumn("Level Contents", ImGuiTableColumnFlags_WidthFixed, 300.0f);
    gui_->TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    gui_->TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthFixed, 400.0f);

    gui_->TableNextRow();

    gui_->TableNextColumn();
    {
      ScopedChild navigator = gui_->CreateScopedChild("LevelEditorNavigator", ImVec2(0.0f, 0.0f));
      if (navigator) RETURN_IF_ERROR(RenderNavigator());
    }

    gui_->TableNextColumn();
    RETURN_IF_ERROR(RenderViewport());

    gui_->TableNextColumn();
    {
      ScopedChild inspector = gui_->CreateScopedChild("LevelEditorInspector", ImVec2(0.0f, 0.0f));
      if (inspector) RETURN_IF_ERROR(RenderInspector());
    }
  }

  if (!render_palette) return absl::OkStatus();
  gui_->InvisibleButton("##PlacementPaletteResize",
                        ImVec2(gui_->GetContentRegionAvail().x, kPanelGap));
  if (gui_->IsItemActive()) {
    palette_height_ = std::clamp(palette_height_ - gui_->GetIO().MouseDelta.y,
                                 kMinimumPaletteHeight, kMaximumUserPaletteHeight);
  }
  ScopedChild palette =
      gui_->CreateScopedChild("LevelEditorPalette", ImVec2(0.0f, layout.palette_height), true);
  if (!palette) return absl::OkStatus();
  RETURN_IF_ERROR(RenderPalette());

  return absl::OkStatus();
}

absl::Status LevelEditor::RenderPalette() {
  const LevelAuthoringReadiness readiness = CurrentReadiness();
  if (!readiness.can_place()) {
    gui_->Text("PLACEMENT PALETTE");
    gui_->TextDisabled("Complete these requirements before placing content:");
    for (const std::string& blocker : readiness.placement_blockers) {
      gui_->TextDisabled("- %s", blocker.c_str());
    }
    return absl::OkStatus();
  }
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

  // A level is one authored world, not a container of collapsible scenes.
  Level& level = *level_model_.active_level();

  gui_->Text("LEVEL CONTENTS");
  gui_->TextDisabled("Level: %s", level.name.empty() ? "(unnamed)" : level.name.c_str());
  gui_->Separator();

  if (gui_->Selectable("Level Settings##level_settings",
                       selection_.type == SelectionState::Type::kLevel)) {
    selection_.Clear();
    selection_.type = SelectionState::Type::kLevel;
  }

  gui_->Spacing();
  gui_->Separator();
  gui_->Text("WORLD LAYERS");
  RETURN_IF_ERROR(world_layer_panel_->RenderNavigator(level, world_layer_model_, selection_));

  gui_->Spacing();
  gui_->Separator();
  gui_->Text("PARALLAX ZONES");
  gui_->TextDisabled("Zones choose reusable backgrounds for world regions.");
  std::optional<int> previous_zone_id;
  if (selection_.type == SelectionState::Type::kZone) {
    previous_zone_id = selection_.zone_id;
  }
  RETURN_IF_ERROR(parallax_zone_panel_->RenderNavigator(level, selection_));

  if (selection_.type == SelectionState::Type::kZone && previous_zone_id != selection_.zone_id) {
    if (const ParallaxZone* zone = FindParallaxZoneById(level.zones, selection_.zone_id);
        zone != nullptr) {
      viewport_tab_->FrameZone(*zone);
      viewport_tab_->SetParallaxPreviewMode(ParallaxPreviewMode::kSelectedZone);
    }
  }

  return absl::OkStatus();
}

absl::Status LevelEditor::RenderInspector() {
  if (!level_model_.has_active_level()) {
    gui_->Text("INSPECTOR");
    gui_->Separator();
    gui_->TextDisabled("No Level Loaded");
    return absl::OkStatus();
  }

  switch (selection_.type) {
    case SelectionState::Type::kNone:
      gui_->Text("INSPECTOR");
      gui_->Separator();
      gui_->TextDisabled("Select an item to view properties.");
      break;

    case SelectionState::Type::kLevel: {
      gui_->Text("Level > Settings");
      gui_->Separator();
      ASSIGN_OR_RETURN(LevelPanelEvent event, level_panel_->RenderDetails(level_model_));
      RETURN_IF_ERROR(HandleLevelPanelEvent(event));
      const LevelAuthoringReadiness readiness = CurrentReadiness();
      if (!readiness.can_save()) {
        gui_->Separator();
        gui_->TextColored({1.0f, 0.65f, 0.2f, 1.0f}, "Required before saving:");
        for (const std::string& blocker : readiness.save_blockers) {
          gui_->TextWrapped("- %s", blocker.c_str());
        }
      }
    } break;

    case SelectionState::Type::kWorldLayer: {
      const WorldLayer* layer =
          FindWorldLayer(*level_model_.active_level(), selection_.world_layer_id);
      gui_->Text("Level > World Layers > %s",
                 layer == nullptr || layer->name.empty() ? "(unnamed layer)" : layer->name.c_str());
      gui_->Separator();
      RETURN_IF_ERROR(world_layer_panel_->RenderDetails(*level_model_.active_level(),
                                                        world_layer_model_, selection_));
    } break;

    case SelectionState::Type::kZone: {
      const ParallaxZone* selected_zone =
          FindParallaxZoneById(level_model_.active_level()->zones, selection_.zone_id);
      gui_->Text("Level > Parallax Zones > %s",
                 selected_zone == nullptr || selected_zone->name.empty()
                     ? "(unnamed zone)"
                     : selected_zone->name.c_str());
      gui_->Separator();
      if (gui_->Button("Frame Zone")) {
        const Level& level = *level_model_.active_level();
        if (const ParallaxZone* zone = FindParallaxZoneById(level.zones, selection_.zone_id);
            zone != nullptr) {
          viewport_tab_->FrameZone(*zone);
        }
      }
      RETURN_IF_ERROR(
          parallax_zone_panel_->RenderDetails(*level_model_.active_level(), selection_));
      theme_request_ = parallax_zone_panel_->TakeThemeRequest();
    } break;

    case SelectionState::Type::kZoneCreation: {
      gui_->Text("Level > Parallax Zones > New Zone");
      gui_->Separator();
      const std::optional<int> created =
          parallax_zone_panel_->RenderCreation(*level_model_.active_level(), selection_);
      if (created.has_value()) {
        const ParallaxZone* zone =
            FindParallaxZoneById(level_model_.active_level()->zones, *created);
        if (zone != nullptr) {
          viewport_tab_->FrameZone(*zone);
          viewport_tab_->SetParallaxPreviewMode(ParallaxPreviewMode::kSelectedZone);
        }
      }
    } break;

    case SelectionState::Type::kEntity: {
      gui_->Text("Level > Entity");
      gui_->Separator();
      Level& level = *level_model_.active_level();
      Entity* entity = FindEntity(level, selection_.entity_id);
      WorldLayer* entity_layer = FindEntityLayer(level, selection_.entity_id);
      if (entity == nullptr || entity_layer == nullptr) {
        selection_.Clear();
        break;
      }

      const uint64_t entity_id = entity->id;
      RenderInspectorSection(*gui_, "IDENTITY", "Stable entity and authored blueprint references.");
      {
        InspectorPropertyGrid grid(*gui_, "EntityIdentity");
        if (grid.BeginRow("Entity ID", "Stable identity unique within this level.")) {
          gui_->Text("%" PRIu64, entity_id);
        }
        if (!entity->blueprint_id.empty() &&
            grid.BeginRow("Blueprint", "Asset that defines this entity's authored states.")) {
          gui_->TextWrapped("%s", entity->blueprint_id.c_str());
        }
      }

      const bool locked = world_layer_model_.IsLocked(entity_layer->id);
      ScopedDisabled locked_controls = gui_->CreateScopedDisabled(locked);
      RenderInspectorSection(*gui_, "TRANSFORM", "Position of the entity origin in world pixels.");
      float pos_x = static_cast<float>(entity->transform.position.x);
      float pos_y = static_cast<float>(entity->transform.position.y);
      {
        InspectorPropertyGrid grid(*gui_, "EntityTransform");
        if (grid.BeginRow("X (px)", "Horizontal entity origin.")) {
          if (gui_->InputFloat("##entity_x", &pos_x, 1.0f, 16.0f, "%.1f")) {
            entity->transform.position.x = pos_x;
          }
        }
        if (grid.BeginRow("Y (px)", "Vertical entity origin.")) {
          if (gui_->InputFloat("##entity_y", &pos_y, 1.0f, 16.0f, "%.1f")) {
            entity->transform.position.y = pos_y;
          }
        }
      }

      // Higher draws later among entities in this world layer. Moving content
      // across terrain depth is a layer operation, not a sort-order trick.
      RenderInspectorSection(
          *gui_, "COMPOSITION",
          "World layers control terrain depth. Draw order only sorts entities within one layer.");
      int sort_order = entity->sort_order;
      {
        InspectorPropertyGrid grid(*gui_, "EntityComposition");
        if (grid.BeginRow("World layer", "Depth slice that owns this entity.")) {
          if (ScopedCombo combo =
                  gui_->CreateScopedCombo("##entity_world_layer", entity_layer->name.c_str());
              combo) {
            for (const WorldLayer& candidate : level.layers) {
              const bool selected = candidate.id == entity_layer->id;
              ScopedDisabled destination_locked =
                  gui_->CreateScopedDisabled(world_layer_model_.IsLocked(candidate.id));
              if (gui_->Selectable(candidate.name.c_str(), selected)) {
                RETURN_IF_ERROR(MoveEntityToLayer(level, entity_id, candidate.id));
                RETURN_IF_ERROR(world_layer_model_.Activate(level, candidate.id));
                entity_layer = FindWorldLayer(level, candidate.id);
                entity = FindEntity(level, entity_id);
              }
              if (selected) gui_->SetItemDefaultFocus();
            }
          }
        }
        if (grid.BeginRow("Draw order", "Higher values draw later within the world layer.")) {
          if (gui_->InputInt("##entity_draw_order", &sort_order)) {
            entity->sort_order = sort_order;
          }
        }
      }

      RenderInspectorSection(*gui_, "PLACEMENT", "Explicit alignment actions for this entity.");
      if (!entity->blueprint_id.empty() && gui_->Button("Resnap to Grid")) {
        ASSIGN_OR_RETURN(Blueprint * blueprint, api_->GetBlueprint(entity->blueprint_id));
        if (blueprint == nullptr) {
          return absl::FailedPreconditionError("selected entity resolved to a null blueprint");
        }
        const std::optional<int> state_index = blueprint->state_index(entity->blueprint_state_key);
        if (!state_index.has_value()) {
          return absl::InvalidArgumentError(absl::StrCat("selected entity ", entity_id,
                                                         " has invalid blueprint state key '",
                                                         entity->blueprint_state_key, "'"));
        }

        ASSIGN_OR_RETURN(
            const Vec snapped_origin,
            SnapBlueprintOriginToNearestGridAnchor(
                entity->transform.position, level.tile_render_width, level.tile_render_height,
                blueprint->states[*state_index].placement_mode));
        entity->transform.position = snapped_origin;
      }

      RenderInspectorSection(*gui_, "DANGER ZONE", "Removing deletes this entity from the level.");
      if (gui_->Button("Remove Entity")) {
        entity_layer->entities.erase(entity_id);
        selection_.Clear();
      }
      break;
    }
  }
  return absl::OkStatus();
}

std::optional<ParallaxZonePanel::ThemeRequest> LevelEditor::TakeThemeRequest() {
  std::optional<ParallaxZonePanel::ThemeRequest> request = std::move(theme_request_);
  theme_request_.reset();
  return request;
}

absl::Status LevelEditor::AssignThemeToZone(int zone_id, const std::string& theme_id) {
  Level* level = level_model_.active_level();
  if (level == nullptr) return absl::FailedPreconditionError("No level is open.");
  ParallaxZone* zone = nullptr;
  for (ParallaxZone& candidate : level->zones) {
    if (candidate.id == zone_id) {
      zone = &candidate;
      break;
    }
  }
  if (zone == nullptr) return absl::NotFoundError("The requested parallax zone is no longer open.");
  RETURN_IF_ERROR(api_->GetParallaxTheme(theme_id).status());
  zone->theme_id = theme_id;
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

void LevelEditor::RenderDerivedArtworkStatus() {
  if (!derived_terrain_.is_open()) return;
  const DerivedTerrainSession::ArtworkStatus status = derived_terrain_.artwork_status();

  if (status.unsaved == 0) {
    gui_->TextDisabled("Artwork: %d tiles, %dx%d atlas.", status.tiles, status.atlas_width,
                       status.atlas_height);
    return;
  }
  // Coloured only when something is pending, because that is the state a user
  // can lose: the atlas grows in memory and nothing reaches disk until save.
  gui_->TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Artwork: %d tiles, %dx%d atlas, %d unsaved.",
                    status.tiles, status.atlas_width, status.atlas_height, status.unsaved);
}

absl::Status LevelEditor::RenderViewport() {
  gui_->Text("WORLD VIEWPORT");
  gui_->Separator();

  ScopedChild viewport = gui_->CreateScopedChild("LevelViewport", ImVec2(0, 0), true);
  if (!viewport) return absl::OkStatus();

  Level* level = level_model_.active_level();
  if (level == nullptr) {
    gui_->TextDisabled("No level selected.");
    return absl::OkStatus();
  }
  if (!std::isfinite(level->width) || !std::isfinite(level->height) || level->width <= 0.0 ||
      level->height <= 0.0) {
    gui_->TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "LEVEL SETTINGS REQUIRED");
    gui_->TextWrapped(
        "Enter a positive, tile-aligned world width and height in the Settings inspector.");
    if (selection_.type != SelectionState::Type::kLevel && gui_->Button("Go to Level Settings")) {
      selection_.Clear();
      selection_.type = SelectionState::Type::kLevel;
    }
    return absl::OkStatus();
  }
  world_layer_model_.Reconcile(*level);
  WorldLayer* active_world_layer = world_layer_model_.active_layer(*level);
  if (active_world_layer == nullptr) {
    return absl::FailedPreconditionError("level viewport has no active world layer");
  }
  const bool active_world_layer_editable = world_layer_model_.IsVisible(active_world_layer->id) &&
                                           !world_layer_model_.IsLocked(active_world_layer->id);

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
  RenderDerivedArtworkStatus();

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

  const std::vector<ParallaxTheme> parallax_themes = api_->GetAllParallaxThemes();
  const absl::Status rendered = viewport_tab_->Render({
      .level = level,
      .active_world_layer_id = active_world_layer->id,
      .hidden_world_layer_ids = &world_layer_model_.hidden_layer_ids(),
      .active_world_layer_editable = active_world_layer_editable,
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
      .parallax_themes = &parallax_themes,
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
    active_world_layer->entities.erase(*delete_request);
  }

  std::optional<Entity> new_entity = viewport_tab_->TakeNewEntity();
  if (new_entity.has_value()) {
    RETURN_IF_ERROR(level->AddEntity(active_world_layer->id, std::move(*new_entity)));
  }

  std::optional<uint64_t> click_selection = viewport_tab_->TakeClickSelection();
  if (click_selection.has_value()) {
    selection_.ApplyEntityPick(*click_selection);
  }

  return absl::OkStatus();
}

}  // namespace zebes
