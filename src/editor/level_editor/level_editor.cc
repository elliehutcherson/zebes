#include "editor/level_editor/level_editor.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "editor/level_editor/level_panel.h"
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
    ASSIGN_OR_RETURN(level_panel_, LevelPanel::Create({
                                       .api = api_,
                                   }));
  }
  if (options.parallax_panel) {
    parallax_panel_ = std::move(options.parallax_panel);
  } else {
    ASSIGN_OR_RETURN(parallax_panel_, ParallaxPanel::Create({.api = api_}));
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

    RETURN_IF_ERROR(RenderLeft());

    ImGui::TableNextColumn();

    RenderCenter();

    ImGui::TableNextColumn();

    RenderRight();

    ImGui::EndTable();
  }
  return absl::OkStatus();
}

absl::Status LevelEditor::RenderLeft() {
  if (!editting_level_.has_value()) {
    ASSIGN_OR_RETURN(LevelResult result, level_panel_->Render(editting_level_));
    return absl::OkStatus();
  }

  const float height = ImGui::GetContentRegionAvail().y * 0.5f;

  if (auto level_child = ScopedChild("LevelPanel", ImVec2(0, height)); level_child) {
    ASSIGN_OR_RETURN(LevelResult result, level_panel_->Render(editting_level_));
  }

  if (!editting_level_.has_value()) {
    return absl::OkStatus();
  }

  ImGui::Separator();

  if (auto parallax_child = ScopedChild("ParallaxPanel", ImVec2(0, 0)); parallax_child) {
    ASSIGN_OR_RETURN(ParallaxResult unused, parallax_panel_->Render(*editting_level_));
  }

  return absl::OkStatus();
}

void LevelEditor::RenderCenter() {
  ImGui::Text("Viewport");
  ImGui::Separator();

  auto tab_default = []() {
    if (auto tab_item = ScopedTabItem("Viewport"); tab_item) {
      // TODO(ellie): Render the selected level's tilemap and entities here.
      ImGui::TextDisabled("(Placeholder: Level Viewport)");
    }
  };

  auto tab_parallax = [this]() {
    if (!parallax_panel_->GetEditingLayer().has_value()) return;

    std::string tab_name = "Parallax Layer";

    if (auto tab_item = ScopedTabItem(tab_name.c_str()); tab_item) {
      std::optional<std::string> texture = parallax_panel_->GetTexture();

      if (!texture.has_value()) {
        ImGui::Text("No texture selected...");
      } else {
        ImGui::Text("Texture ID: %s", texture->c_str());
      }
    }
  };

  // Using a child window for the viewport to clip content if needed and handle scrolling.
  if (auto view_child = ScopedChild("LevelViewport", ImVec2(0, 0), true); view_child) {
    if (auto view_tab = ScopedTabBar("ViewportTabs"); view_tab) {
      tab_default();
      tab_parallax();
    }
  }
}

void LevelEditor::RenderRight() {
  ImGui::Text("Details");
  ImGui::Separator();
  // TODO(ellie): Render inspector for the selected level or entity.
  ImGui::TextDisabled("(Placeholder: Level Properties)");
}

}  // namespace zebes
