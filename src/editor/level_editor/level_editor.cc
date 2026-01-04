#include "editor/level_editor/level_editor.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<LevelEditor>> LevelEditor::Create(Options options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null");
  }
  auto editor = absl::WrapUnique(new LevelEditor(options.api));
  RETURN_IF_ERROR(editor->Init(std::move(options)));
  return editor;
}

LevelEditor::LevelEditor(Api* api) : api_(api) {}

absl::Status LevelEditor::Init(Options options) {
  if (options.level_panel) {
    level_panel_ = std::move(options.level_panel);
  } else {
    ASSIGN_OR_RETURN(level_panel_, LevelPanel::Create(api_));
  }
  return absl::OkStatus();
}

absl::Status LevelEditor::Render() {
  // Use a table with 3 columns for the layout: Left (List), Center (Viewport), Right (Details).
  // Stretch sizing allows the viewport to take the majority of the available space.
  if (ImGui::BeginTable("LevelEditorLayout", 3,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_SizingStretchProp)) {
    // Setup columns with relative sizing
    ImGui::TableSetupColumn("Level List", ImGuiTableColumnFlags_WidthStretch, 0.2f);
    ImGui::TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch, 0.6f);
    ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 0.2f);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    RenderLeft();

    ImGui::TableNextColumn();

    RenderCenter();

    ImGui::TableNextColumn();

    RenderRight();

    ImGui::EndTable();
  }
  return absl::OkStatus();
}

void LevelEditor::RenderLeft() {
  if (!level_panel_) {
    return;
  }
  absl::StatusOr<LevelResult> result = level_panel_->Render();
  if (!result.ok()) {
  }
  // TODO(ellie): handle result (Attach/Detach) if needed by Editor.
  // For now LevelPanel handles its own state (list vs detail).
}

void LevelEditor::RenderCenter() {
  ImGui::Text("Viewport");
  ImGui::Separator();
  // Using a child window for the viewport to clip content if needed and handle scrolling.
  ImGui::BeginChild("LevelViewport", ImVec2(0, 0), true);
  // TODO(ellie): Render the selected level's tilemap and entities here.
  ImGui::TextDisabled("(Placeholder: Level Viewport)");
  ImGui::EndChild();
}

void LevelEditor::RenderRight() {
  ImGui::Text("Details");
  ImGui::Separator();
  // TODO(ellie): Render inspector for the selected level or entity.
  ImGui::TextDisabled("(Placeholder: Level Properties)");
}

}  // namespace zebes
