#include "editor/level_editor/blueprint_palette_panel.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<BlueprintPalettePanel>> BlueprintPalettePanel::Create(
    Options options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null.");
  }
  if (options.gui == nullptr) {
    return absl::InvalidArgumentError("Gui must not be null.");
  }
  return absl::WrapUnique(new BlueprintPalettePanel(std::move(options)));
}

BlueprintPalettePanel::BlueprintPalettePanel(Options options)
    : api_(*options.api), gui_(options.gui) {}

absl::Status BlueprintPalettePanel::Render() {
  auto child = ScopedChild(gui_, "BlueprintPalette", ImVec2(0, 70), true);
  if (!child) return absl::OkStatus();

  std::vector<Blueprint> blueprints = api_.GetAllBlueprints();
  if (blueprints.empty()) {
    gui_->TextDisabled("No blueprints loaded.");
    return absl::OkStatus();
  }

  gui_->Checkbox("Snap to Grid", &snap_to_grid_);
  gui_->SameLine();
  gui_->Checkbox("Show Entity Borders", &show_entity_borders_);
  gui_->SameLine();

  for (const Blueprint& bp : blueprints) {
    const bool is_selected = (selected_blueprint_ != nullptr && selected_blueprint_->id == bp.id);

    // Highlight the active blueprint button.
    ScopedStyleColor highlight =
        is_selected
            ? gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 1.0f, 1.0f))
            : gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.26f, 0.26f, 1.0f));

    std::string label = absl::StrCat(bp.name, "##bp_", bp.id);
    if (gui_->Button(label.c_str())) {
      // Toggle off if clicking the already-selected blueprint.
      if (is_selected) {
        selected_blueprint_ = nullptr;
      } else {
        // Find the stable pointer via the Api.
        absl::StatusOr<Blueprint*> bp_ptr = api_.GetBlueprint(bp.id);
        if (bp_ptr.ok()) {
          selected_blueprint_ = *bp_ptr;
        }
      }
    }
    gui_->SameLine();
  }

  return absl::OkStatus();
}

}  // namespace zebes
