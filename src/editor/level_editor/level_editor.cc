#include "editor/level_editor/level_editor.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "editor/level_editor/level_panel.h"
#include "editor/level_editor/level_panel_interface.h"
#include "editor/level_editor/parallax_panel.h"
#include "editor/level_editor/parallax_preview_tab.h"
#include "imgui.h"

namespace zebes {
namespace {

constexpr ImGuiTableFlags kTableFlags =
    ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;

}  // namespace

absl::StatusOr<std::unique_ptr<LevelEditor>> LevelEditor::Create(Options options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null");
  }
  if (options.gui == nullptr) {
    return absl::InvalidArgumentError("Gui must not be null");
  }
  auto editor = absl::WrapUnique(new LevelEditor(options.api, options.gui));
  RETURN_IF_ERROR(editor->Init(std::move(options)));
  return editor;
}

LevelEditor::LevelEditor(Api* api, GuiInterface* gui) : api_(api), gui_(gui) {}

absl::Status LevelEditor::Init(Options options) {
  if (options.level_panel) {
    level_panel_ = std::move(options.level_panel);
  } else {
    ASSIGN_OR_RETURN(level_panel_, LevelPanel::Create({
                                       .api = api_,
                                       .gui = gui_,
                                   }));
  }
  if (options.parallax_panel) {
    parallax_panel_ = std::move(options.parallax_panel);
  } else {
    ASSIGN_OR_RETURN(parallax_panel_, ParallaxPanel::Create({.api = api_, .gui = gui_}));
  }

  parallax_tab_ = std::make_unique<ParallaxPreviewTab>(*api_, gui_);
  viewport_tab_ = std::make_unique<ViewportTab>(*api_, gui_);

  return absl::OkStatus();
}

absl::Status LevelEditor::Render() {
  // Use a table with 3 columns for the layout: Left (List), Center (Viewport), Right (Details).
  // Stretch sizing allows the viewport to take the majority of the available space.
  if (ScopedTable table = gui_->CreateScopedTable("LevelEditorLayout", 3, kTableFlags); table) {
    // Setup columns with relative sizing
    gui_->TableSetupColumn("Level List", ImGuiTableColumnFlags_WidthStretch, 0.2f);
    gui_->TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch, 0.6f);
    gui_->TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 0.2f);

    gui_->TableNextRow();
    gui_->TableNextColumn();

    RETURN_IF_ERROR(RenderLeft());

    gui_->TableNextColumn();

    RETURN_IF_ERROR(RenderCenter());

    gui_->TableNextColumn();

    RenderRight();

    // EndTable is handled by ScopedTable dtorette
  }
  return absl::OkStatus();
}

absl::Status LevelEditor::RenderLeft() {
  const float height =
      gui_->GetContentRegionAvail().y * (editting_level_.has_value() ? 0.5f : 1.0f);

  LevelResult lvl_result;
  if (auto level_child = ScopedChild(gui_, "LevelPanel", ImVec2(0, height)); level_child) {
    ASSIGN_OR_RETURN(lvl_result, level_panel_->Render(editting_level_));
  }

  if (!editting_level_.has_value()) {
    parallax_panel_->Reset();
    return absl::OkStatus();
  }

  gui_->Separator();

  if (auto parallax_child = ScopedChild(gui_, "ParallaxPanel", ImVec2(0, 0)); parallax_child) {
    ASSIGN_OR_RETURN(ParallaxResult unused, parallax_panel_->Render(*editting_level_));
  }

  return absl::OkStatus();
}

absl::Status LevelEditor::RenderCenter() {
  gui_->Text("Viewport");
  gui_->Separator();

  auto tab_default = [this]() -> absl::Status {
    if (auto tab_item = ScopedTabItem(gui_, "Viewport"); tab_item) {
      if (editting_level_.has_value()) {
        RETURN_IF_ERROR(viewport_tab_->Render(*editting_level_));
      } else {
        gui_->TextDisabled("No level selected.");
      }
    }
    return absl::OkStatus();
  };

  auto tab_parallax = [this]() -> absl::Status {
    std::optional<ParallaxLayer>& layer = parallax_panel_->GetEditingLayer();
    if (!layer.has_value()) return absl::OkStatus();
    std::optional<std::string> texture_id =
        layer->texture_id.empty() ? std::nullopt : std::make_optional(layer->texture_id);
    return parallax_tab_->Render(texture_id);
  };

  // Using a child window for the viewport to clip content if needed and handle scrolling.
  if (auto view_child = ScopedChild(gui_, "LevelViewport", ImVec2(0, 0), true); view_child) {
    if (auto view_tab = ScopedTabBar(gui_, "ViewportTabs"); view_tab) {
      RETURN_IF_ERROR(tab_default());
      RETURN_IF_ERROR(tab_parallax());
    }
  }

  return absl::OkStatus();
}

void LevelEditor::RenderRight() {
  gui_->Text("Details");
  gui_->Separator();
  // TODO(ellie): Render inspector for the selected level or entity.
  gui_->TextDisabled("(Placeholder: Level Properties)");
}

}  // namespace zebes
