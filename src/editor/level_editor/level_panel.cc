#include "editor/level_editor/level_panel.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <memory>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"
#include "objects/level.h"

namespace zebes {
namespace {

// The display name of a level in the catalog, or its ID when the catalog no
// longer holds it. A confirmation has to name what it will destroy, and naming
// the wrong thing is worse than naming it awkwardly.
std::string LevelDisplayName(const LevelPanelModel& model, const std::string& level_id) {
  for (const auto& catalog_entry : model.levels()) {
    if (catalog_entry.second.id != level_id) continue;
    return catalog_entry.second.name.empty() ? "(unnamed level)" : catalog_entry.second.name;
  }
  return level_id;
}

}  // namespace

absl::StatusOr<std::unique_ptr<LevelPanel>> LevelPanel::Create(GuiInterface* gui) {
  if (gui == nullptr) return absl::InvalidArgumentError("Gui can not be null.");
  return absl::WrapUnique(new LevelPanel(gui));
}

LevelPanel::LevelPanel(GuiInterface* gui) : gui_(gui) {}

absl::StatusOr<LevelPanelEvent> LevelPanel::RenderList(LevelPanelModel& model) {
  if (gui_->Button("New Level")) {
    delete_level_prompt_.Disarm();
    model.BeginNewLevel();
    return LevelPanelEvent{.action = LevelPanelAction::kNew};
  }
  gui_->SameLine();

  // Disabled rather than guarded: a button that is enabled, does nothing when
  // pressed, and reports nothing teaches that the editor is broken.
  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(!model.has_level_selection());
    if (gui_->Button("Edit")) {
      delete_level_prompt_.Disarm();
      RETURN_IF_ERROR(model.BeginEditingSelectedLevel());
      return LevelPanelEvent{.action = LevelPanelAction::kOpen};
    }
  }
  gui_->SameLine();

  {
    const std::string question =
        absl::StrCat("Delete '", LevelDisplayName(model, model.selected_level_id()),
                     "'? Every tile placed in it goes too.");
    ScopedDisabled disabled =
        gui_->CreateScopedDisabled(!model.has_level_selection() && !delete_level_prompt_.armed());
    if (delete_level_prompt_.Render(*gui_, "Delete", model.selected_level_id(), question,
                                    "Level")) {
      return LevelPanelEvent{.action = LevelPanelAction::kDelete};
    }
  }

  if (ScopedListBox list_box = gui_->CreateScopedListBox("##Levels", ImVec2(-FLT_MIN, -FLT_MIN));
      list_box) {
    for (const auto& catalog_entry : model.levels()) {
      const Level& level = catalog_entry.second;
      const bool is_selected = model.selected_level_id() == level.id;
      const std::string label =
          absl::StrCat(level.name.empty() ? "(unnamed level)" : level.name, "##level_", level.id);
      if (gui_->Selectable(label.c_str(), is_selected)) {
        RETURN_IF_ERROR(model.SelectLevel(level.id));
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  return LevelPanelEvent{};
}

absl::Status LevelPanel::RenderTilesetField(LevelPanelModel& model, const Level& level,
                                            InspectorPropertyGrid& grid) {
  // A level's tiles are bare IDs into this tileset, so it is the one setting
  // here that can invalidate everything already painted.
  const std::string current = model.ActiveTilesetName();
  if (!grid.BeginRow("Tileset", "Artwork catalog used by every painted tile in this level.")) {
    return absl::OkStatus();
  }
  if (ScopedCombo combo = gui_->CreateScopedCombo(
          "##level_tileset", current.empty() ? "(none selected)" : current.c_str());
      combo) {
    for (const TilesetChoice& choice : model.tileset_choices()) {
      const bool is_selected = choice.id == level.tileset_id;
      ScopedId id = gui_->CreateScopedId(choice.id.c_str());
      if (gui_->Selectable(choice.name.c_str(), is_selected)) {
        RETURN_IF_ERROR(model.RequestTilesetChange(choice.id));
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  if (!model.has_pending_tileset_change()) return absl::OkStatus();
  return RenderTilesetChangeConfirmation(model);
}

absl::Status LevelPanel::RenderTilesetChangeConfirmation(LevelPanelModel& model) {
  // Switching cannot carry the tiles across: their IDs name different artwork
  // in the new tileset, so the choice is discard-or-keep, not discard-or-not.
  gui_->TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                    "Switching tilesets discards this level's %d placed tile(s).",
                    model.placed_tile_count());

  {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Discard tiles and switch")) {
      RETURN_IF_ERROR(model.ConfirmTilesetChange());
      return absl::OkStatus();
    }
  }
  gui_->SameLine();
  if (gui_->Button("Keep tileset")) model.CancelTilesetChange();
  return absl::OkStatus();
}

absl::StatusOr<LevelPanelEvent> LevelPanel::RenderToolbar(
    LevelPanelModel& model, const LevelAuthoringReadiness& readiness) {
  if (!model.has_active_level()) return LevelPanelEvent{};

  // Leaving is only destructive when there is something to lose.
  if (discard_edits_prompt_.armed()) {
    if (discard_edits_prompt_.Render(*gui_, "Discard and Close", model.selected_level_id(),
                                     "Discard unsaved changes to this level?", "CloseLevel")) {
      model.CloseActiveLevel();
      return LevelPanelEvent{.action = LevelPanelAction::kClose};
    }
  } else if (gui_->Button("Close Level")) {
    if (!model.has_unsaved_changes()) {
      model.CloseActiveLevel();
      return LevelPanelEvent{.action = LevelPanelAction::kClose};
    }
    discard_edits_prompt_.Arm(model.selected_level_id());
  }

  gui_->SameLine();
  {
    ScopedDisabled blocked = gui_->CreateScopedDisabled(!readiness.can_save());
    if (gui_->Button(model.is_new_level() ? "Create Level" : "Save Level")) {
      discard_edits_prompt_.Disarm();
      return LevelPanelEvent{.action = model.is_new_level() ? LevelPanelAction::kCreate
                                                            : LevelPanelAction::kSave};
    }
  }
  if (!readiness.can_save()) {
    gui_->SameLine();
    const std::string label =
        absl::StrCat("Review ", readiness.save_blockers.size(),
                     readiness.save_blockers.size() == 1 ? " issue" : " issues");
    if (gui_->Button(label.c_str())) {
      return LevelPanelEvent{.action = LevelPanelAction::kReviewIssues};
    }
  }
  return LevelPanelEvent{};
}

absl::StatusOr<LevelPanelEvent> LevelPanel::RenderDetails(LevelPanelModel& model) {
  Level* level = model.active_level();
  if (level == nullptr) return absl::FailedPreconditionError("No level is being edited");

  RenderInspectorSection(*gui_, "IDENTITY", "The display name used in the level catalog.");
  {
    InspectorPropertyGrid grid(*gui_, "LevelIdentity");
    if (grid.BeginRow("Name", "Editable display name; this does not change the resource ID.")) {
      gui_->InputText("##level_name", &level->name);
    }
  }

  RenderInspectorSection(
      *gui_, "WORLD SIZE",
      "The complete editable world rectangle, measured in world pixels. Dimensions must align "
      "to the world tile size.");
  {
    InspectorPropertyGrid grid(*gui_, "LevelWorldSize");
    const double width_step = std::max(1, level->tile_render_width);
    const double height_step = std::max(1, level->tile_render_height);
    if (grid.BeginRow("Width (px)", "Horizontal world extent.")) {
      gui_->InputDouble("##world_width", &level->width, width_step, width_step * 10.0, "%.0f");
    }
    if (grid.BeginRow("Height (px)", "Vertical world extent.")) {
      gui_->InputDouble("##world_height", &level->height, height_step, height_step * 10.0, "%.0f");
    }
  }
  if (level->width > 0.0 && level->height > 0.0 && level->tile_render_width > 0 &&
      level->tile_render_height > 0) {
    const double columns = level->width / level->tile_render_width;
    const double rows = level->height / level->tile_render_height;
    const bool aligned = std::fmod(level->width, level->tile_render_width) == 0.0 &&
                         std::fmod(level->height, level->tile_render_height) == 0.0;
    if (aligned) {
      gui_->TextDisabled("Tile grid: %.0f columns x %.0f rows", columns, rows);
    } else {
      gui_->TextColored({1.0f, 0.65f, 0.2f, 1.0f},
                        "Tile grid: %.2f columns x %.2f rows (must be whole)", columns, rows);
    }
  }
  const bool can_frame_world = std::isfinite(level->width) && std::isfinite(level->height) &&
                               level->width > 0.0 && level->height > 0.0;
  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(!can_frame_world);
    if (gui_->Button("Frame World")) {
      return LevelPanelEvent{.action = LevelPanelAction::kFrameWorld};
    }
  }

  RenderInspectorSection(
      *gui_, "RENDERING",
      "The tileset supplies artwork. World tile size controls grid spacing and rendered cell "
      "size independently of the source atlas.");
  {
    InspectorPropertyGrid grid(*gui_, "LevelRendering");
    RETURN_IF_ERROR(RenderTilesetField(model, *level, grid));
    if (grid.BeginRow("Tile width (px)", "Width of one tile cell in the level world.")) {
      gui_->InputInt("##tile_width", &level->tile_render_width, 1, 16);
    }
    if (grid.BeginRow("Tile height (px)", "Height of one tile cell in the level world.")) {
      gui_->InputInt("##tile_height", &level->tile_render_height, 1, 16);
    }
  }

  RenderInspectorSection(*gui_, "PLAYER START",
                         "Initial player position in world pixels, measured from the top-left.");
  {
    InspectorPropertyGrid grid(*gui_, "LevelSpawn");
    if (grid.BeginRow("X (px)", "Horizontal player start position.")) {
      gui_->InputDouble("##spawn_x", &level->spawn_point.x, 1.0, 16.0, "%.0f");
    }
    if (grid.BeginRow("Y (px)", "Vertical player start position.")) {
      gui_->InputDouble("##spawn_y", &level->spawn_point.y, 1.0, 16.0, "%.0f");
    }
  }

  if (gui_->CollapsingHeader("Advanced##LevelSetup")) {
    InspectorPropertyGrid grid(*gui_, "LevelAdvanced");
    if (grid.BeginRow("Resource ID", "Stable identity used by references and persistence.")) {
      gui_->InputText("##level_id", &level->id, ImGuiInputTextFlags_ReadOnly);
    }
  }

  return LevelPanelEvent{};
}

}  // namespace zebes
