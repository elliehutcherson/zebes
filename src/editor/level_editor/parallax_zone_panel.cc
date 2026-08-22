#include "editor/level_editor/parallax_zone_panel.h"

#include <algorithm>
#include <cmath>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<ParallaxZonePanel>> ParallaxZonePanel::Create(Options options) {
  if (options.gui == nullptr) {
    return absl::InvalidArgumentError("Gui can not be null.");
  }
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("Api can not be null.");
  }

  return absl::WrapUnique(new ParallaxZonePanel(std::move(options)));
}

ParallaxZonePanel::ParallaxZonePanel(Options options) : api_(options.api), gui_(options.gui) {}

absl::Status ParallaxZonePanel::RenderNavigator(Level& level, SelectionState& selection) {
  const std::vector<ParallaxTheme> themes = api_->GetAllParallaxThemes();
  const bool can_add_zone = std::isfinite(level.width) && std::isfinite(level.height) &&
                            level.width > 0.0 && level.height > 0.0;
  gui_->BeginDisabled(!can_add_zone);
  const bool add_zone = gui_->Button("Add Zone");
  gui_->EndDisabled();

  if (!can_add_zone) {
    gui_->TextDisabled("Set a positive level width and height before adding a zone.");
  }

  if (add_zone && can_add_zone) {
    int new_id = 0;
    for (const ParallaxZone& z : level.zones) {
      new_id = std::max(new_id, z.id + 1);
    }
    ParallaxZone new_zone = {
        .id = new_id,
        .name = absl::StrCat("Zone ", level.zones.size()),
        .min_point = {.x = 0, .y = 0},
        .max_point = {.x = std::min(256.0, level.width), .y = std::min(256.0, level.height)},
        .fade_length = {.x = 0, .y = 0},
    };
    level.zones.push_back(new_zone);

    selection.type = SelectionState::Type::kZone;
    selection.zone_id = new_id;
  }

  for (const ParallaxZone& zone : level.zones) {
    std::string label = zone.name.empty() ? "(unnamed zone)" : zone.name;

    auto theme_it = std::find_if(themes.begin(), themes.end(), [&](const ParallaxTheme& theme) {
      return theme.id == zone.theme_id;
    });
    if (theme_it != themes.end()) {
      absl::StrAppend(&label, " (", theme_it->name.empty() ? "unnamed theme" : theme_it->name, ")");
    }
    absl::StrAppend(&label, "##zone_", zone.id);

    bool is_selected =
        (selection.type == SelectionState::Type::kZone && selection.zone_id == zone.id);
    if (gui_->Selectable(label.c_str(), is_selected)) {
      selection.type = SelectionState::Type::kZone;
      selection.zone_id = zone.id;
    }
  }
  return absl::OkStatus();
}

absl::Status ParallaxZonePanel::RenderDetails(Level& level, SelectionState& selection) {
  auto zone_it =
      std::find_if(level.zones.begin(), level.zones.end(),
                   [&](const ParallaxZone& zone) { return zone.id == selection.zone_id; });
  if (zone_it == level.zones.end()) {
    selection.Clear();
    return absl::InvalidArgumentError("Selected zone does not exist.");
  }

  ParallaxZone& zone = *zone_it;

  gui_->TextDisabled("Zone Properties");
  gui_->Separator();

  gui_->BeginDisabled();
  gui_->InputInt("Id", &zone.id);
  gui_->EndDisabled();

  gui_->InputText("Name", &zone.name);

  const std::vector<ParallaxTheme> themes = api_->GetAllParallaxThemes();
  // Blank preview when the zone names a theme the catalog no longer has,
  // rather than showing a stale name.
  const char* theme_preview = "";
  auto preview_it = std::find_if(themes.begin(), themes.end(), [&](const ParallaxTheme& theme) {
    return theme.id == zone.theme_id;
  });
  if (preview_it != themes.end()) {
    theme_preview = preview_it->name.c_str();
  }
  if (auto combo = gui_->CreateScopedCombo("Theme", theme_preview); combo) {
    for (const ParallaxTheme& theme : themes) {
      bool is_selected = (zone.theme_id == theme.id);
      const std::string label =
          absl::StrCat(theme.name.empty() ? "(unnamed theme)" : theme.name, "##theme_", theme.id);
      if (gui_->Selectable(label.c_str(), is_selected)) {
        zone.theme_id = theme.id;
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  const bool has_theme = preview_it != themes.end();
  {
    ScopedDisabled no_theme = gui_->CreateScopedDisabled(!has_theme);
    if (gui_->Button("Edit Theme")) {
      theme_request_ = ThemeRequest{ThemeAction::kEdit, zone.id, zone.theme_id};
    }
    gui_->SameLine();
    if (gui_->Button("Duplicate and Assign")) {
      theme_request_ = ThemeRequest{ThemeAction::kDuplicateAndAssign, zone.id, zone.theme_id};
    }
  }

  gui_->Separator();
  gui_->Text("Boundaries");
  gui_->InputDouble("Min X", &zone.min_point.x);
  gui_->InputDouble("Min Y", &zone.min_point.y);
  gui_->InputDouble("Max X", &zone.max_point.x);
  gui_->InputDouble("Max Y", &zone.max_point.y);

  // Clamped every frame rather than validated on save, so a typed-in value is
  // corrected as it is entered.
  zone.min_point.x = std::clamp(zone.min_point.x, 0.0, level.width);
  zone.min_point.y = std::clamp(zone.min_point.y, 0.0, level.height);
  zone.max_point.x = std::clamp(zone.max_point.x, 0.0, level.width);
  zone.max_point.y = std::clamp(zone.max_point.y, 0.0, level.height);

  gui_->Separator();
  gui_->Text("Transition");
  gui_->TextWrapped(
      "Zone fades are not rendered yet. Values are preserved but cannot be edited "
      "until the two-theme compositor is implemented.");
  gui_->BeginDisabled();
  gui_->InputDouble("Fade X (unsupported)", &zone.fade_length.x);
  gui_->InputDouble("Fade Y (unsupported)", &zone.fade_length.y);
  gui_->EndDisabled();
  if ((zone.fade_length.x != 0.0 || zone.fade_length.y != 0.0) &&
      gui_->Button("Reset Fades to Zero")) {
    zone.fade_length = {};
  }

  gui_->Spacing();
  {
    ScopedStyleColor color =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete Zone")) {
      level.zones.erase(zone_it);
      selection.Clear();
    }
  }

  return absl::OkStatus();
}

std::optional<ParallaxZonePanel::ThemeRequest> ParallaxZonePanel::TakeThemeRequest() {
  std::optional<ThemeRequest> request = std::move(theme_request_);
  theme_request_.reset();
  return request;
}

}  // namespace zebes
