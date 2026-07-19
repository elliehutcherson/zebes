#include "editor/blueprint_editor/collider_panel.h"

#include <cfloat>
#include <cstddef>
#include <memory>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/editor_utils.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "objects/collider.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<ColliderPanel>> ColliderPanel::Create(GuiInterface* gui) {
  if (gui == nullptr) return absl::InvalidArgumentError("GUI can not be null.");
  return absl::WrapUnique(new ColliderPanel(gui));
}

ColliderPanel::ColliderPanel(GuiInterface* gui) : gui_(gui) {}

absl::StatusOr<ColliderPanel::Action> ColliderPanel::Render(ColliderPanelModel& model) {
  ScopedId id = gui_->CreateScopedId("ColliderPanel");

  gui_->Text("Collider");
  gui_->Separator();

  if (model.has_active_collider()) return RenderDetails(model);
  return RenderList(model);
}

absl::StatusOr<ColliderPanel::Action> ColliderPanel::RenderList(ColliderPanelModel& model) {
  if (gui_->Button("Create")) {
    model.BeginNewCollider();
    return Action::kCreate;
  }
  gui_->SameLine();

  if (gui_->Button("Attach") && model.has_collider_selection()) {
    RETURN_IF_ERROR(model.BeginEditingSelectedCollider());
    return Action::kAttach;
  }
  gui_->SameLine();

  {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete") && model.has_collider_selection()) return Action::kDelete;
  }

  if (auto list_box = gui_->CreateScopedListBox("Colliders", ImVec2(-FLT_MIN, -FLT_MIN));
      list_box) {
    for (const auto& catalog_entry : model.colliders()) {
      const Collider& collider = catalog_entry.second;
      const bool is_selected = model.selected_collider_id() == collider.id;
      if (gui_->Selectable(collider.name_id().c_str(), is_selected)) {
        RETURN_IF_ERROR(model.SelectCollider(collider.id));
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  return Action::kNone;
}

absl::StatusOr<ColliderPanel::Action> ColliderPanel::RenderDetails(ColliderPanelModel& model) {
  Collider* collider = model.active_collider();
  if (collider == nullptr) return absl::FailedPreconditionError("No collider is being edited");

  gui_->Text("ID: %s", collider->id.empty() ? "<auto>" : collider->id.c_str());
  gui_->InputText("Name", &collider->name);

  RETURN_IF_ERROR(RenderPolygonList(model));

  gui_->Separator();
  const float button_width = CalculateButtonWidth(gui_, /*num_buttons=*/3);

  if (gui_->Button("Save", ImVec2(button_width, 0))) return Action::kSave;
  gui_->SameLine();

  {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
    if (gui_->Button("Detach", ImVec2(button_width, 0))) {
      model.CloseActiveCollider();
      return Action::kDetach;
    }
  }
  gui_->SameLine();

  {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    if (gui_->Button("Reset", ImVec2(button_width, 0))) {
      RETURN_IF_ERROR(model.ResetActiveCollider());
    }
  }

  return Action::kNone;
}

absl::Status ColliderPanel::RenderPolygonList(ColliderPanelModel& model) {
  gui_->Separator();
  gui_->Text("Polygons");
  gui_->SameLine();
  if (gui_->Button("Add Polygon")) RETURN_IF_ERROR(model.AddPolygon());

  Collider* collider = model.active_collider();
  if (collider == nullptr) return absl::FailedPreconditionError("No collider is being edited");

  std::size_t polygon_index = 0;
  while (polygon_index < collider->polygons.size()) {
    ScopedId id = gui_->CreateScopedId(static_cast<int>(polygon_index));
    ASSIGN_OR_RETURN(bool deleted, RenderPolygonDetails(model, polygon_index));
    if (!deleted) ++polygon_index;
  }
  return absl::OkStatus();
}

absl::StatusOr<bool> ColliderPanel::RenderPolygonDetails(ColliderPanelModel& model,
                                                          std::size_t polygon_index) {
  Collider* collider = model.active_collider();
  if (collider == nullptr || polygon_index >= collider->polygons.size()) {
    return absl::OutOfRangeError("Polygon index is out of range");
  }

  gui_->Text("Polygon %d", static_cast<int>(polygon_index));
  gui_->SameLine();
  if (gui_->Button("X")) {
    RETURN_IF_ERROR(model.DeletePolygon(polygon_index));
    return true;
  }

  std::size_t vertex_index = 0;
  while (vertex_index < collider->polygons[polygon_index].size()) {
    ScopedId id = gui_->CreateScopedId(static_cast<int>(vertex_index));
    Vec& vertex = collider->polygons[polygon_index][vertex_index];
    gui_->SetNextItemWidth(100);
    gui_->InputDouble("##vec_x", &vertex.x);
    gui_->SameLine();
    gui_->SetNextItemWidth(100);
    gui_->InputDouble("##vec_y", &vertex.y);
    gui_->SameLine();
    if (gui_->Button("X", ImVec2(30, 0))) {
      RETURN_IF_ERROR(model.DeleteVertex(polygon_index, vertex_index));
      continue;
    }
    ++vertex_index;
  }

  if (gui_->Button("Add Vertex")) RETURN_IF_ERROR(model.AddVertex(polygon_index));
  return false;
}

absl::StatusOr<bool> ColliderPanel::RenderCanvas(ColliderPanelModel& model, Canvas& canvas,
                                                  bool input_allowed) {
  SyncCanvas(model);
  if (canvas_collider_ == nullptr) return false;
  return canvas_collider_->Render(canvas, input_allowed);
}

void ColliderPanel::SyncCanvas(ColliderPanelModel& model) {
  if (!model.has_active_collider()) {
    canvas_collider_.reset();
    canvas_revision_ = model.active_revision();
    return;
  }
  if (canvas_collider_ != nullptr && canvas_revision_ == model.active_revision()) return;

  canvas_collider_ = std::make_unique<CanvasCollider>(*model.active_collider());
  canvas_revision_ = model.active_revision();
}

}  // namespace zebes
