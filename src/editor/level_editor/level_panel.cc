#include "editor/level_editor/level_panel.h"

#include <cfloat>
#include <memory>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
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
      if (gui_->Selectable(level.name.c_str(), is_selected)) {
        RETURN_IF_ERROR(model.SelectLevel(level.id));
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  return LevelPanelEvent{};
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
