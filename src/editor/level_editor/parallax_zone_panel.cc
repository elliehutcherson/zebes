#include "editor/level_editor/parallax_zone_panel.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<ParallaxZonePanel>> ParallaxZonePanel::Create(Options options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("API can not be null.");
  }
  if (options.gui == nullptr) {
    return absl::InvalidArgumentError("Gui can not be null.");
  }

  return absl::WrapUnique(new ParallaxZonePanel(std::move(options)));
}

ParallaxZonePanel::ParallaxZonePanel(Options options) : api_(*options.api), gui_(options.gui) {}

absl::Status ParallaxZonePanel::RenderNavigator(Level& level, SelectionState& selection) {
  if (gui_->Button("Add Zone")) {
    ParallaxZone new_zone = {
        .min_point = {.x = 0, .y = 0},
        .max_point = {.x = 256, .y = 256},
        .fade_length = {.x = 10, .y = 10},
    };
    level.zones.push_back(new_zone);

    selection.type = SelectionState::Type::kZone;
    selection.zone_index = level.zones.size() - 1;
  }

  for (int i = 0; i < level.zones.size(); ++i) {
    std::string label = absl::StrCat("Zone ", i);

    // Add theme name to label if it exists
    if (!level.zones[i].theme_id) {
      absl::StrAppend(&label, " (", level.zones[i].theme_id, ")");
    }

    bool is_selected = (selection.type == SelectionState::Type::kZone && selection.zone_index == i);
    if (gui_->Selectable(label.c_str(), is_selected)) {
      selection.type = SelectionState::Type::kZone;
      selection.zone_index = i;
    }
  }
  return absl::OkStatus();
}

absl::Status ParallaxZonePanel::RenderDetails(Level& level, SelectionState& selection) {
  if (selection.zone_index < 0 || selection.zone_index >= level.zones.size()) {
    selection.Clear();
    return absl::InvalidArgumentError("Selection zone index out of bounds.");
  }

  ParallaxZone& zone = level.zones[selection.zone_index];

  gui_->TextDisabled("Zone Properties");
  gui_->Separator();

  // Theme Selector
  if (auto combo = gui_->CreateScopedCombo("Theme", "none"); combo) {
    for (const auto& [id, theme] : level.themes) {
      bool is_selected = (zone.theme_id == id);
      if (gui_->Selectable(theme.name.c_str(), is_selected)) {
        zone.theme_id = id;
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  gui_->Separator();
  gui_->Text("Boundaries");
  gui_->InputDouble("Min X", &zone.min_point.x);
  gui_->InputDouble("Min Y", &zone.min_point.y);
  gui_->InputDouble("Max X", &zone.max_point.x);
  gui_->InputDouble("Max Y", &zone.max_point.y);

  gui_->Separator();
  gui_->Text("Transition");
  gui_->InputDouble("Fade X", &zone.fade_length.x);
  gui_->InputDouble("Fade Y", &zone.fade_length.y);

  gui_->Spacing();
  {
    ScopedStyleColor color =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete Zone")) {
      level.zones.erase(level.zones.begin() + selection.zone_index);
      selection.Clear();
    }
  }

  return absl::OkStatus();
}

}  // namespace zebes
