#include "editor/blueprint/blueprint_editor.h"

#include <memory>

#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/animator.h"
#include "editor/blueprint/blueprint_panel.h"
#include "editor/blueprint/collider_panel.h"
#include "editor/blueprint/sprite_panel.h"
#include "editor/canvas.h"
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
  animator_ = std::make_unique<Animator>();

  // Initialize panels
  ASSIGN_OR_RETURN(blueprint_panel_, BlueprintPanel::Create(api_));
  ASSIGN_OR_RETURN(blueprint_state_panel_, BlueprintStatePanel::Create(api_));
  ASSIGN_OR_RETURN(collider_panel_, ColliderPanel::Create(api_));
  ASSIGN_OR_RETURN(sprite_panel_, SpritePanel::Create(api_));
  return absl::OkStatus();
}

void BlueprintEditor::ExitBlueprintStateMode() {
  LOG(INFO) << __func__;
  blueprint_state_panel_->Reset();

  // Clean up panels
  collider_panel_->Clear();
  sprite_panel_->Reset();

  canvas_.Reset();
  mode_ = Mode::kBlueprint;
}

void BlueprintEditor::Render() {
  if (!ImGui::BeginTable(/*str_id=*/"BlueprintEditorTable", /*columns=*/3,
                         /*flags=*/ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable,
                         /*outer_size=*/ImGui::GetContentRegionAvail())) {
    return;
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
  RenderLeftPanel();

  // 2. Editor Column (The Canvas)
  ImGui::TableNextColumn();
  RenderCanvas();

  // 3. Details Column
  ImGui::TableNextColumn();
  RenderRightPanel();

  ImGui::EndTable();
}

void BlueprintEditor::RenderLeftPanel() {
  if (mode_ == Mode::kBlueprint) {
    RenderBlueprintListMode();
  } else {
    RenderBlueprintStateMode();
  }
}

void BlueprintEditor::RenderBlueprintListMode() {
  int state_index = blueprint_panel_->Render();
  if (state_index == -1) return;

  Blueprint* bp = blueprint_panel_->GetBlueprint();
  if (bp == nullptr) {
    LOG(FATAL) << "Blueprint is null!!!!";
    return;
  }

  EnterBlueprintStateMode(*bp, state_index);
}

void BlueprintEditor::EnterBlueprintStateMode(Blueprint& bp, int state_index) {
  mode_ = Mode::kBlueprintState;

  blueprint_state_panel_->SetState(bp, state_index);

  // Set Collider
  std::optional<std::string> collider_id = bp.collider_id(state_index);
  if (collider_id.has_value()) {
    collider_panel_->SetCollider(*collider_id);
    // CanvasCollider is managed by ColliderPanel
  }

  // Set Sprite
  std::optional<std::string> sprite_id = bp.sprite_id(state_index);
  if (sprite_id.has_value()) {
    sprite_panel_->SetSprite(*sprite_id);
    sprite_panel_->SetAttachedSprite(*sprite_id);
  }
}

void BlueprintEditor::RenderBlueprintStateMode() {
  if (ImGui::Button("Back")) {
    ExitBlueprintStateMode();
    return;
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

  ColliderResult collider_result = collider_panel_->Render();
  UpdateStateCollider(collider_result);
}

void BlueprintEditor::RenderRightPanel() {
  if (mode_ == Mode::kBlueprint) {
    ImGui::Text("Select Blueprint State to view sprites.");
    return;
  }

  SpriteResult sprite_result = sprite_panel_->Render();
  UpdateStateSprite(sprite_result);
}

void BlueprintEditor::RenderCanvas() {
  if (mode_ == Mode::kBlueprint) {
    ImGui::Text("Select Blueprint State to view canvas.");
    return;
  }

  ImVec2 size = ImGui::GetContentRegionAvail();
  // New Canvas API: Begin takes the size and handles background/child window
  canvas_.Begin("StateCanvas", size);

  // Handles pan (MMB) and Zoom (Wheel) automatically
  canvas_.HandleInput();

  // Rulers & Grid
  canvas_.DrawGrid();

  // Sync Sprite Logic
  if (auto* sprite = sprite_panel_->GetCanvasSprite()) {
    absl::StatusOr<bool> drag =
        sprite->Render(&canvas_, sprite_panel_->GetFrameIndex(), /*input_allowed=*/true);
    LOG_IF_ERROR(drag.status());
  }

  // Draw objects
  if (auto* collider = collider_panel_->GetCanvasCollider()) {
    LOG_IF_ERROR(collider->Render(&canvas_, /*input_allowed=*/true).status());
  }

  canvas_.End();
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

    // Ensure the panel creates the CanvasSprite
    sprite_panel_->SetAttachedSprite(sprite_result.id);

  } else if (sprite_result.type == SpriteResult::Type::kDetach) {
    bp->states[state_index].sprite_id.clear();
    sprite_panel_->SetAttachedSprite(std::nullopt);
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
