#include "editor/level_editor/level_panel.h"

#include <cfloat>
#include <memory>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"
#include "objects/level.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<LevelPanel>> LevelPanel::Create(GuiInterface* gui) {
  if (gui == nullptr) return absl::InvalidArgumentError("Gui can not be null.");
  return absl::WrapUnique(new LevelPanel(gui));
}

LevelPanel::LevelPanel(GuiInterface* gui) : gui_(gui) {}

absl::StatusOr<LevelPanelEvent> LevelPanel::RenderList(LevelPanelModel& model) {
  if (gui_->Button("Create")) {
    model.BeginNewLevel();
    return LevelPanelEvent{.action = LevelPanelAction::kCreate};
  }
  gui_->SameLine();

  if (gui_->Button("Edit") && model.has_level_selection()) {
    RETURN_IF_ERROR(model.BeginEditingSelectedLevel());
    return LevelPanelEvent{.action = LevelPanelAction::kOpen};
  }
  gui_->SameLine();

  {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete") && model.has_level_selection()) {
      return LevelPanelEvent{.action = LevelPanelAction::kDelete};
    }
  }

  if (ScopedListBox list_box = gui_->CreateScopedListBox("##Levels", ImVec2(-FLT_MIN, -FLT_MIN));
      list_box) {
    for (const auto& catalog_entry : model.levels()) {
      const Level& level = catalog_entry.second;
      const bool is_selected = model.selected_level_id() == level.id;
      const std::string label = absl::StrCat(
          level.name.empty() ? "(unnamed level)" : level.name, "##level_", level.id);
      if (gui_->Selectable(label.c_str(), is_selected)) {
        RETURN_IF_ERROR(model.SelectLevel(level.id));
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  return LevelPanelEvent{};
}

absl::Status LevelPanel::RenderTilesetField(LevelPanelModel& model, const Level& level) {
  // A level's tiles are bare IDs into this tileset, so it is the one setting
  // here that can invalidate everything already painted.
  const std::string current = model.ActiveTilesetName();
  if (ScopedCombo combo = gui_->CreateScopedCombo(
          "Tileset", current.empty() ? "(none)" : current.c_str());
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

absl::StatusOr<LevelPanelEvent> LevelPanel::RenderDetails(LevelPanelModel& model) {
  Level* level = model.active_level();
  if (level == nullptr) return absl::FailedPreconditionError("No level is being edited");

  if (gui_->Button("Back")) {
    model.CloseActiveLevel();
    return LevelPanelEvent{.action = LevelPanelAction::kClose};
  }

  gui_->SameLine();
  if (gui_->Button("Save")) {
    return LevelPanelEvent{.action = LevelPanelAction::kSave};
  }

  gui_->Separator();
  gui_->Text("Details");

  gui_->InputText("ID", &level->id, ImGuiInputTextFlags_ReadOnly);
  gui_->InputText("Name", &level->name);
  RETURN_IF_ERROR(RenderTilesetField(model, *level));
  gui_->InputDouble("Width", &level->width);
  gui_->InputDouble("Height", &level->height);
  gui_->InputInt("Tile Render Width", &level->tile_render_width);
  gui_->InputInt("Tile Render Height", &level->tile_render_height);

  gui_->Text("Spawn Point");
  gui_->InputDouble("X", &level->spawn_point.x);
  gui_->SameLine();
  gui_->InputDouble("Y", &level->spawn_point.y);

  return LevelPanelEvent{};
}

}  // namespace zebes
