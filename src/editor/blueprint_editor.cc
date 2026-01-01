#include "editor/blueprint_editor.h"

#include <memory>

#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/animator.h"
#include "editor/blueprint_panel.h"
#include "editor/canvas_collider.h"
#include "editor/collider_panel.h"
#include "editor/sprite_panel.h"
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
  collider_panel_->Clear();
  sprite_panel_->Reset();
  canvas_.Reset();
  mode_ = Mode::kBlueprint;
  canvas_collider_.reset();
  canvas_sprite_.reset();
  sprite_panel_->SetAttachedSprite(std::nullopt);
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
    int state_index = blueprint_panel_->Render();
    if (state_index == -1) return;

    Blueprint* bp = blueprint_panel_->GetBlueprint();
    if (bp == nullptr) {
      LOG(ERROR) << "Blueprint is null!!!!";
      return;
    }

    mode_ = Mode::kBlueprintState;

    blueprint_state_panel_->SetState(bp, state_index);
    std::optional<std::string> collider_id = bp->collider_id(state_index);
    if (collider_id.has_value()) {
      collider_panel_->SetCollider(*collider_id);
      canvas_collider_ = CanvasCollider(&canvas_);
      canvas_collider_->SetCollider(collider_panel_->GetCollider());
    }

    std::optional<std::string> sprite_id = bp->sprite_id(state_index);
    if (sprite_id.has_value()) {
      sprite_panel_->SetSprite(*sprite_id);
      sprite_panel_->SetAttachedSprite(*sprite_id);
      canvas_sprite_ = CanvasSprite(&canvas_);
      if (auto* sprite = sprite_panel_->GetSprite()) {
        canvas_sprite_->SetSprite(*sprite);
      }
    }
  }

  if (mode_ == Mode::kBlueprintState && ImGui::Button("Back")) {
    ExitBlueprintStateMode();
    return;
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
  if (canvas_sprite_.has_value()) {
    // 1. Sync Panel -> Canvas (Source of Truth)
    if (auto* sprite = sprite_panel_->GetSprite()) {
      canvas_sprite_->SetSprite(*sprite);
    }

    // 2. Render & Handle Input
    auto status_or = canvas_sprite_->Render(sprite_panel_->GetFrameIndex(), /*input_allowed=*/true);
    LOG_IF_ERROR(status_or.status());

    // 3. Sync Canvas -> Panel if modified (Drag)
    if (status_or.ok() && status_or.value()) {
      if (auto* sprite = sprite_panel_->GetSprite()) {
        *sprite = canvas_sprite_->GetSprite();
      }
    }
  }

  // Draw objects
  if (canvas_collider_.has_value()) {
    LOG_IF_ERROR(canvas_collider_->Render(/*input_allowed=*/true).status());
  }

  canvas_.End();
}

void BlueprintEditor::UpdateStateCollider(const ColliderResult& collider_result) {
  if (collider_result.type == ColliderResult::Type::kNone) return;

  Blueprint* bp = blueprint_panel_->GetBlueprint();
  if (bp == nullptr) {
    LOG(ERROR) << "Attempted to update state with bad blueprint!!!";
  }
  int state_index = blueprint_state_panel_->GetStateIndex();
  if (state_index == -1) {
    LOG(ERROR) << "Attempted to udpate state with bad state index!!!";
  }

  if (collider_result.type == ColliderResult::Type::kAttach) {
    bp->collider_ids[state_index] = collider_result.collider_id;
    canvas_collider_ = CanvasCollider(&canvas_);
    canvas_collider_->SetCollider(collider_panel_->GetCollider());
  } else if (collider_result.type == ColliderResult::Type::kDetach) {
    bp->collider_ids.erase(state_index);
    canvas_collider_.reset();
  }
}

void BlueprintEditor::UpdateStateSprite(const SpriteResult& sprite_result) {
  if (sprite_result.type == SpriteResult::Type::kNone) return;

  Blueprint* bp = blueprint_panel_->GetBlueprint();
  if (bp == nullptr) {
    LOG(ERROR) << "Attempted to update state with bad blueprint!!!";
  }
  int state_index = blueprint_state_panel_->GetStateIndex();
  if (state_index == -1) {
    LOG(ERROR) << "Attempted to udpate state with bad state index!!!";
  }

  if (sprite_result.type == SpriteResult::Type::kAttach) {
    bp->sprite_ids[state_index] = sprite_result.id;
    canvas_sprite_ = CanvasSprite(&canvas_);
    // We already know GetSprite() is not null because we just attached it.
    // However, for safety we can check.
    if (auto* sprite = sprite_panel_->GetSprite()) {
      canvas_sprite_->SetSprite(*sprite);
    }
  } else if (sprite_result.type == SpriteResult::Type::kDetach) {
    bp->sprite_ids.erase(state_index);
    canvas_sprite_.reset();
  }
}

}  // namespace zebes
