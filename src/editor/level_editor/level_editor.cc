#include "editor/level_editor/level_editor.h"

#include "absl/memory/memory.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<LevelEditor>> LevelEditor::Create(Api* api) {
  if (api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null");
  }
  return absl::WrapUnique(new LevelEditor(api));
}

LevelEditor::LevelEditor(Api* api) : api_(api) {}

LevelEditor::~LevelEditor() {}

void LevelEditor::Render() {
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
}

void LevelEditor::RenderLeft() {
  ImGui::Text("Level List");
  ImGui::Separator();
  ImGui::PushID("LevelListPanel");
  // TODO(ellie): Replace placeholder text with actual level list from API.
  ImGui::TextDisabled("(Placeholder: List of levels)");

  if (ImGui::Button("Add Level")) {
    // TODO(ellie): Implement level creation logic.
  }
  ImGui::PopID();
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
