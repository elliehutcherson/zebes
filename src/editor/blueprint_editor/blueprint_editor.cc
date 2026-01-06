#include "editor/blueprint_editor/blueprint_editor.h"

#include <memory>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/animator.h"
#include "editor/blueprint_editor/blueprint_panel.h"
#include "editor/blueprint_editor/collider_panel.h"
#include "editor/blueprint_editor/sprite_panel.h"
#include "editor/canvas/canvas.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<BlueprintEditor>> BlueprintEditor::Create(Api* api) {
  if (api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null");
  }

  auto ret = std::unique_ptr<BlueprintEditor>(new BlueprintEditor(api));
  RETURN_IF_ERROR(ret->Init());
  return ret;
}

// Constructor: Initializes the editor and refreshes caches.
BlueprintEditor::BlueprintEditor(Api* api) : api_(api), canvas_({.snap_grid = true}) {}

absl::Status BlueprintEditor::Init() {
  mode_ = Mode::kBlueprint;
  animator_ = std::make_unique<Animator>();

  // Initialize panels
  ASSIGN_OR_RETURN(blueprint_panel_, BlueprintPanel::Create(api_));
  ASSIGN_OR_RETURN(blueprint_state_panel_, BlueprintStatePanel::Create(api_));
  ASSIGN_OR_RETURN(collider_panel_, ColliderPanel::Create(api_));
  ASSIGN_OR_RETURN(sprite_panel_, SpritePanel::Create(api_));

  return absl::OkStatus();
}

absl::Status BlueprintEditor::ExitBlueprintStateMode() {
  blueprint_state_panel_->Reset();

  // Clean up panels
  collider_panel_->Detach();
  sprite_panel_->Detach();

  canvas_.Reset();
  mode_ = Mode::kBlueprint;
  return absl::OkStatus();
}

absl::Status BlueprintEditor::Render() {
  if (!ImGui::BeginTable(/*str_id=*/"BlueprintEditorTable", /*columns=*/3,
                         /*flags=*/ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable,
                         /*outer_size=*/ImGui::GetContentRegionAvail())) {
    return absl::OkStatus();
  }

  // Column Setup
  ImGui::TableSetupColumn(/*label=*/"Controls", /*flags=*/ImGuiTableColumnFlags_WidthFixed,
                          /*init_width_or_weight=*/250.0f);
  ImGui::TableSetupColumn(/*label=*/"Editor", /*flags=*/ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(/*label=*/"Sprite Details", /*flags=*/ImGuiTableColumnFlags_WidthFixed,
                          /*init_width_or_weight=*/300.0f);
  ImGui::TableHeadersRow();

  ImGui::TableNextRow();

  // 1. Controls Column
  ImGui::TableNextColumn();
  RETURN_IF_ERROR(RenderLeftPanel());

  // 2. Editor Column (The Canvas)
  ImGui::TableNextColumn();
  RETURN_IF_ERROR(RenderCanvas());

  // 3. Details Column
  ImGui::TableNextColumn();
  RETURN_IF_ERROR(RenderRightPanel());

  ImGui::EndTable();
  return absl::OkStatus();
}

absl::Status BlueprintEditor::RenderLeftPanel() {
  if (mode_ == Mode::kBlueprint) {
    return RenderBlueprintListMode();
  }

  return RenderBlueprintStateMode();
}

absl::Status BlueprintEditor::RenderBlueprintListMode() {
  int state_index = blueprint_panel_->Render();
  if (state_index == -1) return absl::OkStatus();

  Blueprint* bp = blueprint_panel_->GetBlueprint();
  if (bp == nullptr) {
    return absl::InternalError("Blueprint is null!!!");
  }

  return EnterBlueprintStateMode(*bp, state_index);
}

absl::Status BlueprintEditor::EnterBlueprintStateMode(Blueprint& bp, int state_index) {
  mode_ = Mode::kBlueprintState;

  blueprint_state_panel_->SetState(bp, state_index);

  // Set Collider
  std::optional<std::string> collider_id = bp.collider_id(state_index);
  if (collider_id.has_value()) {
    RETURN_IF_ERROR(collider_panel_->Attach(*collider_id));
  }

  // Set Sprite
  std::optional<std::string> sprite_id = bp.sprite_id(state_index);
  if (sprite_id.has_value()) {
    RETURN_IF_ERROR(sprite_panel_->Attach(*sprite_id));
  }

  return absl::OkStatus();
}

absl::Status BlueprintEditor::RenderBlueprintStateMode() {
  if (ImGui::Button("Back")) {
    return ExitBlueprintStateMode();
  }

  ImGui::SameLine();
  ImGui::SameLine();
  if (ImGui::Button("Save")) {
    SaveBlueprint();
  }

  blueprint_state_panel_->Render();
  ImGui::Spacing();
  ImGui::Spacing();
  ImGui::Spacing();

  ASSIGN_OR_RETURN(ColliderResult collider_result, collider_panel_->Render());
  UpdateStateCollider(collider_result);

  return absl::OkStatus();
}

absl::Status BlueprintEditor::RenderRightPanel() {
  if (mode_ == Mode::kBlueprint) {
    ImGui::Text("Select Blueprint State to view sprites.");
    return absl::OkStatus();
  }

  ASSIGN_OR_RETURN(SpriteResult sprite_result, sprite_panel_->Render());
  UpdateStateSprite(sprite_result);
  return absl::OkStatus();
}

absl::Status BlueprintEditor::RenderCanvas() {
  if (mode_ == Mode::kBlueprint) {
    ImGui::Text("Select Blueprint State to view canvas.");
    return absl::OkStatus();
  }

  ImVec2 size = ImGui::GetContentRegionAvail();
  // New Canvas API: Begin takes the size and handles background/child window
  canvas_.Begin("StateCanvas", size);
  auto canvas_end = absl::MakeCleanup([&] { canvas_.End(); });

  // Handles pan (MMB) and Zoom (Wheel) automatically
  canvas_.HandleInput();

  // Rulers & Grid
  canvas_.DrawGrid();

  // Sync Sprite Logic
  RETURN_IF_ERROR(sprite_panel_->RenderCanvas(canvas_, /*input_allowed=*/true).status());

  // Draw objects
  RETURN_IF_ERROR(collider_panel_->RenderCanvas(canvas_, /*input_allowed=*/true).status());

  return absl::OkStatus();
}

void BlueprintEditor::UpdateStateCollider(const ColliderResult& collider_result) {
  if (collider_result.type == ColliderResult::Type::kNone) return;

  Blueprint* bp = blueprint_panel_->GetBlueprint();
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
    // ColliderPanel manages CanvasCollider creation internally upon attachment/SetCollider
  } else if (collider_result.type == ColliderResult::Type::kDetach) {
    bp->states[state_index].collider_id.clear();
  }
}

void BlueprintEditor::UpdateStateSprite(const SpriteResult& sprite_result) {
  if (sprite_result.type == SpriteResult::Type::kNone) return;

  Blueprint* bp = blueprint_panel_->GetBlueprint();
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
  Blueprint* bp = blueprint_panel_->GetBlueprint();
  if (!bp) {
    LOG(FATAL) << "Blueprint is null in state mode!!!";
  }
  absl::Status status = api_->UpdateBlueprint(*bp);
  if (status.ok()) {
    LOG(INFO) << "Saved blueprint: " << bp->name;
    // FIX: Refresh the cache so the panel has the updated data.
    blueprint_panel_->RefreshBlueprintCache();
  } else {
    LOG(ERROR) << "Failed to save: " << status.message();
  }
}

}  // namespace zebes