#include "editor/blueprint_editor/blueprint_editor.h"

#include <memory>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/animator.h"
#include "editor/blueprint_editor/blueprint_panel.h"
#include "editor/blueprint_editor/collider_panel.h"
#include "editor/blueprint_editor/sprite_panel.h"
#include "editor/canvas/canvas.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<BlueprintEditor>> BlueprintEditor::Create(Api* api,
                                                                         GuiInterface* gui) {
  if (api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null");
  }
  if (gui == nullptr) {
    return absl::InvalidArgumentError("GUI must not be null");
  }

  auto ret = std::unique_ptr<BlueprintEditor>(new BlueprintEditor(api, gui));
  RETURN_IF_ERROR(ret->Init());
  return ret;
}

// Constructor: Initializes the editor and refreshes caches.
BlueprintEditor::BlueprintEditor(Api* api, GuiInterface* gui)
    : api_(api), gui_(gui), canvas_({.gui = gui, .snap_grid = true}) {}

absl::Status BlueprintEditor::Init() {
  mode_ = Mode::kBlueprint;
  animator_ = std::make_unique<Animator>();

  // Initialize panels
  ASSIGN_OR_RETURN(blueprint_panel_, BlueprintPanel::Create(gui_));
  ASSIGN_OR_RETURN(blueprint_state_panel_, BlueprintStatePanel::Create(gui_));
  ASSIGN_OR_RETURN(collider_panel_, ColliderPanel::Create(gui_));
  ASSIGN_OR_RETURN(sprite_panel_, SpritePanel::Create(api_, gui_));
  RefreshBlueprintCatalog();
  RefreshColliderCatalog();

  return absl::OkStatus();
}

absl::Status BlueprintEditor::ExitBlueprintStateMode() {
  blueprint_state_panel_->Reset();

  // Clean up panels
  collider_model_.CloseActiveCollider();
  sprite_panel_->Detach();

  camera_ = {};
  mode_ = Mode::kBlueprint;
  return absl::OkStatus();
}

absl::Status BlueprintEditor::Render() {
  ScopedTable table =
      gui_->CreateScopedTable(/*str_id=*/"BlueprintEditorTable", /*columns=*/3,
                              /*flags=*/ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders);
  if (!table) {
    return absl::OkStatus();
  }

  // Column Setup
  gui_->TableSetupColumn(/*label=*/"Controls", /*flags=*/ImGuiTableColumnFlags_WidthFixed,
                         /*init_width_or_weight=*/250.0f);
  gui_->TableSetupColumn(/*label=*/"Editor", /*flags=*/ImGuiTableColumnFlags_WidthStretch);
  gui_->TableSetupColumn(/*label=*/"Sprite Details", /*flags=*/ImGuiTableColumnFlags_WidthFixed,
                         /*init_width_or_weight=*/300.0f);
  gui_->TableHeadersRow();

  gui_->TableNextRow();

  // 1. Controls Column
  gui_->TableNextColumn();
  RETURN_IF_ERROR(RenderLeftPanel());

  // 2. Editor Column (The Canvas)
  gui_->TableNextColumn();
  RETURN_IF_ERROR(RenderCanvas());

  // 3. Details Column
  gui_->TableNextColumn();
  RETURN_IF_ERROR(RenderRightPanel());

  // ImGui::EndTable() handled by ScopedTable
  return absl::OkStatus();
}

absl::Status BlueprintEditor::RenderLeftPanel() {
  if (mode_ == Mode::kBlueprint) {
    return RenderBlueprintListMode();
  }

  return RenderBlueprintStateMode();
}

absl::Status BlueprintEditor::RenderBlueprintListMode() {
  ASSIGN_OR_RETURN(BlueprintPanel::Event event, blueprint_panel_->Render(blueprint_model_));
  return HandleBlueprintPanelEvent(event);
}

absl::Status BlueprintEditor::EnterBlueprintStateMode(Blueprint& bp, int state_index) {
  mode_ = Mode::kBlueprintState;

  blueprint_state_panel_->SetState(bp, state_index);

  // Set Collider
  std::optional<std::string> collider_id = bp.collider_id(state_index);
  if (collider_id.has_value()) {
    RefreshColliderCatalog();
    RETURN_IF_ERROR(collider_model_.BeginEditingCollider(*collider_id));
  }

  // Set Sprite
  std::optional<std::string> sprite_id = bp.sprite_id(state_index);
  if (sprite_id.has_value()) {
    RETURN_IF_ERROR(sprite_panel_->Attach(*sprite_id));
  }

  return absl::OkStatus();
}

absl::Status BlueprintEditor::RenderBlueprintStateMode() {
  if (gui_->Button("Back")) {
    return ExitBlueprintStateMode();
  }

  gui_->SameLine();
  if (gui_->Button("Save")) {
    SaveBlueprint();
  }

  blueprint_state_panel_->Render();
  gui_->Spacing();
  gui_->Spacing();
  gui_->Spacing();

  ASSIGN_OR_RETURN(ColliderPanel::Action action, collider_panel_->Render(collider_model_));
  ASSIGN_OR_RETURN(ColliderResult collider_result, HandleColliderPanelAction(action));
  UpdateStateCollider(collider_result);

  return absl::OkStatus();
}

absl::Status BlueprintEditor::RenderRightPanel() {
  if (mode_ == Mode::kBlueprint) {
    gui_->Text("Select Blueprint State to view sprites.");
    return absl::OkStatus();
  }

  ASSIGN_OR_RETURN(SpriteResult sprite_result, sprite_panel_->Render());
  UpdateStateSprite(sprite_result);
  return absl::OkStatus();
}

absl::Status BlueprintEditor::RenderCanvas() {
  if (mode_ == Mode::kBlueprint) {
    gui_->Text("Select Blueprint State to view canvas.");
    return absl::OkStatus();
  }

  ImVec2 size = gui_->GetContentRegionAvail();
  // New Canvas API: Begin takes the size and handles background/child window
  canvas_.Begin("StateCanvas", size, camera_);
  auto canvas_end = absl::MakeCleanup([&] { canvas_.End(); });

  // Rulers & Grid
  canvas_.DrawGrid();

  // Handles pan (MMB) and Zoom (Wheel) automatically
  canvas_.HandleInput();

  // Sync Sprite Logic
  RETURN_IF_ERROR(sprite_panel_->RenderCanvas(canvas_, /*input_allowed=*/true).status());

  // Draw objects
  RETURN_IF_ERROR(
      collider_panel_->RenderCanvas(collider_model_, canvas_, /*input_allowed=*/true).status());

  return absl::OkStatus();
}

void BlueprintEditor::RefreshBlueprintCatalog() {
  blueprint_model_.SetBlueprints(api_->GetAllBlueprints());
}

absl::Status BlueprintEditor::HandleBlueprintPanelEvent(const BlueprintPanel::Event& event) {
  switch (event.action) {
    case BlueprintPanel::Action::kNone:
      return absl::OkStatus();
    case BlueprintPanel::Action::kSave:
      return SaveActiveBlueprint();
    case BlueprintPanel::Action::kDelete:
      if (!blueprint_model_.has_blueprint_selection()) {
        return absl::FailedPreconditionError("No blueprint is selected");
      }
      RETURN_IF_ERROR(api_->DeleteBlueprint(blueprint_model_.selected_blueprint_id()));
      blueprint_model_.FinishDelete();
      RefreshBlueprintCatalog();
      return absl::OkStatus();
    case BlueprintPanel::Action::kEditState: {
      RETURN_IF_ERROR(blueprint_model_.ValidateStateIndex(event.state_index));
      Blueprint* blueprint = blueprint_model_.active_blueprint();
      if (blueprint == nullptr) {
        return absl::FailedPreconditionError("No blueprint is being edited");
      }
      return EnterBlueprintStateMode(*blueprint, event.state_index);
    }
  }
  return absl::InternalError("Unknown blueprint panel action");
}

absl::Status BlueprintEditor::SaveActiveBlueprint() {
  ASSIGN_OR_RETURN(Blueprint blueprint, blueprint_model_.BuildSaveRequest());
  if (blueprint.id.empty()) {
    ASSIGN_OR_RETURN(std::string id, api_->CreateBlueprint(std::move(blueprint)));
    RETURN_IF_ERROR(blueprint_model_.FinishCreate(id));
  } else {
    RETURN_IF_ERROR(api_->UpdateBlueprint(std::move(blueprint)));
  }
  RefreshBlueprintCatalog();
  return absl::OkStatus();
}

void BlueprintEditor::RefreshColliderCatalog() {
  collider_model_.SetColliders(api_->GetAllColliders());
}

absl::StatusOr<ColliderResult> BlueprintEditor::HandleColliderPanelAction(
    ColliderPanel::Action action) {
  switch (action) {
    case ColliderPanel::Action::kNone:
      return ColliderResult{};
    case ColliderPanel::Action::kCreate: {
      ASSIGN_OR_RETURN(Collider collider, collider_model_.BuildSaveRequest());
      ASSIGN_OR_RETURN(std::string id, api_->CreateCollider(std::move(collider)));
      RETURN_IF_ERROR(collider_model_.FinishCreate(id));
      RefreshColliderCatalog();
      return ColliderResult{.type = ColliderResult::Type::kAttach, .collider_id = std::move(id)};
    }
    case ColliderPanel::Action::kAttach: {
      const Collider* collider = collider_model_.active_collider();
      if (collider == nullptr) {
        return absl::FailedPreconditionError("No collider is available to attach");
      }
      return ColliderResult{.type = ColliderResult::Type::kAttach,
                            .collider_id = collider->id};
    }
    case ColliderPanel::Action::kSave: {
      ASSIGN_OR_RETURN(Collider collider, collider_model_.BuildSaveRequest());
      RETURN_IF_ERROR(api_->UpdateCollider(std::move(collider)));
      RefreshColliderCatalog();
      return ColliderResult{};
    }
    case ColliderPanel::Action::kDelete:
      if (!collider_model_.has_collider_selection()) {
        return absl::FailedPreconditionError("No collider is selected");
      }
      RETURN_IF_ERROR(api_->DeleteCollider(collider_model_.selected_collider_id()));
      collider_model_.FinishDelete();
      RefreshColliderCatalog();
      return ColliderResult{};
    case ColliderPanel::Action::kDetach:
      return ColliderResult{.type = ColliderResult::Type::kDetach};
  }
  return absl::InternalError("Unknown collider panel action");
}

void BlueprintEditor::UpdateStateCollider(const ColliderResult& collider_result) {
  if (collider_result.type == ColliderResult::Type::kNone) return;

  Blueprint* bp = blueprint_model_.active_blueprint();
  if (bp == nullptr) {
    LOG(FATAL) << "Attempted to update state with bad blueprint!!!";
    return;
  }

  int state_index = blueprint_state_panel_->GetStateIndex();
  if (state_index == -1) {
    LOG(ERROR) << "Attempted to udpate state with bad state index!!!";
    return;
  }

  if (collider_result.type == ColliderResult::Type::kAttach) {
    bp->states[state_index].collider_id = collider_result.collider_id;
  } else if (collider_result.type == ColliderResult::Type::kDetach) {
    bp->states[state_index].collider_id.clear();
  }
}

void BlueprintEditor::UpdateStateSprite(const SpriteResult& sprite_result) {
  if (sprite_result.type == SpriteResult::Type::kNone) return;

  Blueprint* bp = blueprint_model_.active_blueprint();
  if (bp == nullptr) {
    LOG(FATAL) << "Attempted to update state with bad blueprint!!!";
    return;
  }

  int state_index = blueprint_state_panel_->GetStateIndex();
  if (state_index == -1) {
    LOG(ERROR) << "Attempted to udpate state with bad state index!!!";
    return;
  }

  if (sprite_result.type == SpriteResult::Type::kAttach) {
    bp->states[state_index].sprite_id = sprite_result.id;
  } else if (sprite_result.type == SpriteResult::Type::kDetach) {
    bp->states[state_index].sprite_id.clear();
  }
}

void BlueprintEditor::SaveBlueprint() {
  Blueprint* blueprint = blueprint_model_.active_blueprint();
  if (blueprint == nullptr) {
    LOG(ERROR) << "No blueprint is active in state mode";
    return;
  }
  const std::string name = blueprint->name;
  absl::Status status = SaveActiveBlueprint();
  if (status.ok()) {
    LOG(INFO) << "Saved blueprint: " << name;
  } else {
    LOG(ERROR) << "Failed to save: " << status.message();
  }
}

}  // namespace zebes
