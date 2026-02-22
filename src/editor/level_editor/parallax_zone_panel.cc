#include "editor/level_editor/parallax_zone_panel.h"

#include <algorithm>

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
    int new_id = 0;
    for (const ParallaxZone& z : level.zones) {
      new_id = std::max(new_id, z.id + 1);
    }
    ParallaxZone new_zone = {
        .id = new_id,
        .name = absl::StrCat("Zone ", level.zones.size()),
        .min_point = {.x = 0, .y = 0},
        .max_point = {.x = std::min(256.0, level.width), .y = std::min(256.0, level.height)},
        .fade_length = {.x = 10, .y = 10},
    };
    level.zones.push_back(new_zone);

    selection.type = SelectionState::Type::kZone;
    selection.zone_index = level.zones.size() - 1;
  }

  for (int i = 0; i < level.zones.size(); ++i) {
    std::string label = absl::StrCat(level.zones[i].name, "##zone_", i);

    // Add theme name to label if it exists
    auto theme_it = level.themes.find(level.zones[i].theme_id);
    if (theme_it != level.themes.end()) {
      absl::StrAppend(&label, " (", theme_it->second.name, ")");
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

  gui_->BeginDisabled();
  gui_->InputInt("Id", &zone.id);
  gui_->EndDisabled();

  gui_->InputText("Name", &zone.name);

  // Theme Selector
  const char* theme_preview = "";
  auto preview_it = level.themes.find(zone.theme_id);
  if (preview_it != level.themes.end()) {
    theme_preview = preview_it->second.name.c_str();
  }
  if (auto combo = gui_->CreateScopedCombo("Theme", theme_preview); combo) {
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

  // Clamp zone boundaries to level bounds.
  zone.min_point.x = std::clamp(zone.min_point.x, 0.0, level.width);
  zone.min_point.y = std::clamp(zone.min_point.y, 0.0, level.height);
  zone.max_point.x = std::clamp(zone.max_point.x, 0.0, level.width);
  zone.max_point.y = std::clamp(zone.max_point.y, 0.0, level.height);

  gui_->Separator();
  gui_->Text("Transition");
  gui_->InputDouble("Fade X", &zone.fade_length.x);
  gui_->InputDouble("Fade Y", &zone.fade_length.y);

  auto theme_it = level.themes.find(zone.theme_id);
  if (theme_it != level.themes.end()) {
    ParallaxTheme& theme = theme_it->second;
    gui_->Separator();
    gui_->Text("Layers");
    for (int i = 0; i < static_cast<int>(theme.layers.size()); ++i) {
      ParallaxLayer& layer = theme.layers[i];
      gui_->Text("%s", layer.name.c_str());
      gui_->InputDouble(absl::StrCat("Offset X##layer_", i).c_str(), &layer.offset.x);
      gui_->InputDouble(absl::StrCat("Offset Y##layer_", i).c_str(), &layer.offset.y);
    }
  }

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
