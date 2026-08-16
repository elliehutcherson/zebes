#include "editor/level_editor/world_layer_panel.h"

#include <cstdint>
#include <memory>
#include <string>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"
#include "objects/entity.h"
#include "objects/level.h"

namespace zebes {
namespace {

int CountTiles(const WorldLayer& layer) {
  int count = 0;
  for (const auto& entry : layer.tile_chunks) {
    for (int tile_id : entry.second.tiles) {
      if (tile_id != 0) ++count;
    }
  }
  return count;
}

}  // namespace

absl::StatusOr<std::unique_ptr<WorldLayerPanel>> WorldLayerPanel::Create(GuiInterface* gui) {
  if (gui == nullptr) return absl::InvalidArgumentError("Gui must not be null.");
  return absl::WrapUnique(new WorldLayerPanel(gui));
}

absl::Status WorldLayerPanel::RenderNavigator(Level& level, WorldLayerModel& model,
                                              SelectionState& selection) {
  model.Reconcile(level);
  if (gui_->Button("Add World Layer")) {
    ASSIGN_OR_RETURN(const int id, model.AddLayer(level));
    selection.Clear();
    selection.type = SelectionState::Type::kWorldLayer;
    selection.world_layer_id = id;
  }

  for (auto layer_it = level.layers.rbegin(); layer_it != level.layers.rend(); ++layer_it) {
    WorldLayer& layer = *layer_it;
    ScopedId scoped_id = gui_->CreateScopedId(layer.id);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
    if (model.active_layer_id() == layer.id) flags |= ImGuiTreeNodeFlags_Selected;

    const std::string label = absl::StrCat(layer.name, "##world_layer");
    const bool open = gui_->CollapsingHeader(label.c_str(), flags);
    if (gui_->IsItemClicked()) {
      RETURN_IF_ERROR(model.Activate(level, layer.id));
      selection.Clear();
      selection.type = SelectionState::Type::kWorldLayer;
      selection.world_layer_id = layer.id;
    }
    if (!open) continue;

    gui_->Indent(10.0f);
    bool visible = model.IsVisible(layer.id);
    if (gui_->Checkbox("Visible", &visible)) {
      RETURN_IF_ERROR(model.SetVisible(level, layer.id, visible));
    }
    gui_->SameLine();
    bool locked = model.IsLocked(layer.id);
    if (gui_->Checkbox("Locked", &locked)) {
      RETURN_IF_ERROR(model.SetLocked(level, layer.id, locked));
    }

    for (const auto& [entity_id, entity] : layer.entities) {
      const std::string entity_label =
          entity.blueprint_id.empty() ? absl::StrCat("Entity ", entity_id)
                                      : absl::StrCat(entity.blueprint_id, " (", entity_id, ")");
      const bool selected =
          selection.type == SelectionState::Type::kEntity && selection.entity_id == entity_id;
      if (gui_->Selectable(entity_label.c_str(), selected)) {
        RETURN_IF_ERROR(model.Activate(level, layer.id));
        selection.Clear();
        selection.type = SelectionState::Type::kEntity;
        selection.entity_id = entity_id;
      }
    }
    gui_->Unindent(10.0f);
  }
  return absl::OkStatus();
}

absl::Status WorldLayerPanel::RenderDetails(Level& level, WorldLayerModel& model,
                                            SelectionState& selection) {
  WorldLayer* layer = FindWorldLayer(level, selection.world_layer_id);
  if (layer == nullptr) {
    selection.Clear();
    return absl::OkStatus();
  }

  gui_->TextDisabled("World Layer Properties");
  gui_->Separator();
  gui_->InputText("Name", &layer->name);
  gui_->Text("%d painted tile(s), %zu entity(s)", CountTiles(*layer), layer->entities.size());

  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(!model.CanMoveForward(level, layer->id));
    if (gui_->Button("Move Forward")) RETURN_IF_ERROR(model.MoveForward(level, layer->id));
  }
  gui_->SameLine();
  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(!model.CanMoveBackward(level, layer->id));
    if (gui_->Button("Move Backward")) RETURN_IF_ERROR(model.MoveBackward(level, layer->id));
  }

  const int layer_id = layer->id;
  const std::string target = absl::StrCat(layer_id);
  const std::string question =
      absl::StrCat("Delete world layer '", layer->name, "'? This removes ", CountTiles(*layer),
                   " painted tile(s) and ", layer->entities.size(), " entity(s).");
  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(level.layers.size() <= 1);
    ScopedStyleColor color =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (delete_prompt_.Render(*gui_, "Delete World Layer", target, question, "WorldLayer")) {
      RETURN_IF_ERROR(model.DeleteLayer(level, layer_id));
      selection.Clear();
      selection.type = SelectionState::Type::kWorldLayer;
      selection.world_layer_id = model.active_layer_id();
    }
  }
  return absl::OkStatus();
}

}  // namespace zebes
