#include "editor/level_editor/parallax_zone_panel.h"

#include <algorithm>
#include <cmath>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "editor/level_editor/level_authoring_readiness.h"
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
  const bool zone_theme_references_resolve =
      std::all_of(level.zones.begin(), level.zones.end(), [&](const ParallaxZone& zone) {
        return std::any_of(themes.begin(), themes.end(),
                           [&](const ParallaxTheme& theme) { return theme.id == zone.theme_id; });
      });
  const LevelAuthoringReadiness readiness = EvaluateLevelAuthoringReadiness(
      level, /*tileset_resolves=*/true, /*active_world_layer_available=*/true,
      /*parallax_theme_available=*/!themes.empty(), zone_theme_references_resolve);
  const bool can_add_zone = readiness.can_add_parallax_zone();
  gui_->BeginDisabled(!can_add_zone);
  const bool add_zone = gui_->Button("Add Parallax Zone...");
  gui_->EndDisabled();

  if (!can_add_zone) {
    if (!readiness.save_blockers.empty()) {
      gui_->TextDisabled("Unavailable until Level Settings is complete.");
    } else if (themes.empty()) {
      gui_->TextDisabled("Create a Parallax Theme before adding a zone.");
    } else {
      gui_->TextDisabled("Resolve the missing theme reference shown below.");
    }
    if (!readiness.save_blockers.empty() && gui_->Button("Go to Level Settings")) {
      selection.Clear();
      selection.type = SelectionState::Type::kLevel;
    }
  }

  if (add_zone && can_add_zone) {
    RETURN_IF_ERROR(creation_model_.Begin(level));
    creation_error_.reset();
    selection.Clear();
    selection.type = SelectionState::Type::kZoneCreation;
  }

  for (const ParallaxZone& zone : level.zones) {
    std::string label = zone.name.empty() ? "(unnamed zone)" : zone.name;

    auto theme_it = std::find_if(themes.begin(), themes.end(), [&](const ParallaxTheme& theme) {
      return theme.id == zone.theme_id;
    });
    if (theme_it != themes.end()) {
      absl::StrAppend(&label, " - ", theme_it->name.empty() ? "unnamed theme" : theme_it->name);
    } else {
      absl::StrAppend(&label, " - MISSING THEME");
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

absl::Status ParallaxZonePanel::RenderThemePicker(InspectorPropertyGrid& grid,
                                                  std::string& theme_id,
                                                  const std::vector<ParallaxTheme>& themes) {
  if (grid.BeginRow("Find theme", "Filter reusable parallax themes by display name or ID.")) {
    gui_->InputText("##theme_search", &theme_search_);
  }
  std::string preview = "(choose theme)";
  for (const ParallaxTheme& theme : themes) {
    if (theme.id == theme_id) preview = theme.name.empty() ? "(unnamed theme)" : theme.name;
  }
  if (!grid.BeginRow("Theme", "Reusable background composition assigned to this zone.")) {
    return absl::OkStatus();
  }
  if (auto combo = gui_->CreateScopedCombo("##parallax_theme", preview.c_str()); combo) {
    for (const ParallaxTheme& theme : themes) {
      if (!theme_search_.empty() && !absl::StrContainsIgnoreCase(theme.name, theme_search_) &&
          !absl::StrContainsIgnoreCase(theme.id, theme_search_)) {
        continue;
      }
      const bool selected = theme_id == theme.id;
      const std::string item =
          absl::StrCat(theme.name.empty() ? "(unnamed theme)" : theme.name, "##theme_", theme.id);
      if (gui_->Selectable(item.c_str(), selected)) theme_id = theme.id;
      if (selected) gui_->SetItemDefaultFocus();
    }
  }
  return absl::OkStatus();
}

std::optional<int> ParallaxZonePanel::RenderCreation(Level& level, SelectionState& selection) {
  ParallaxZone* draft = creation_model_.draft();
  if (draft == nullptr) {
    selection.Clear();
    selection.type = SelectionState::Type::kLevel;
    return std::nullopt;
  }

  gui_->TextWrapped("Choose a theme and bounds. The zone is not added until Create Zone succeeds.");
  RenderInspectorSection(*gui_, "IDENTITY", "The name shown in the Level Contents hierarchy.");
  {
    InspectorPropertyGrid grid(*gui_, "NewZoneIdentity");
    if (grid.BeginRow("Name", "Display name for this parallax region.")) {
      gui_->InputText("##zone_name", &draft->name);
    }
  }
  const std::vector<ParallaxTheme> themes = api_->GetAllParallaxThemes();
  RenderInspectorSection(*gui_, "BACKGROUND",
                         "Theme artwork remains a standalone asset; this zone stores its ID.");
  {
    InspectorPropertyGrid grid(*gui_, "NewZoneTheme");
    const absl::Status picker_status = RenderThemePicker(grid, draft->theme_id, themes);
    if (!picker_status.ok()) creation_error_ = picker_status.message();
  }
  RenderInspectorSection(*gui_, "WORLD BOUNDS",
                         "Rectangle in world pixels where this background can become active.");
  {
    InspectorPropertyGrid grid(*gui_, "NewZoneBounds");
    if (grid.BeginRow("Minimum X (px)", "Left edge, inclusive.")) {
      gui_->InputDouble("##zone_min_x", &draft->min_point.x, 1.0, 16.0, "%.0f");
    }
    if (grid.BeginRow("Minimum Y (px)", "Top edge, inclusive.")) {
      gui_->InputDouble("##zone_min_y", &draft->min_point.y, 1.0, 16.0, "%.0f");
    }
    if (grid.BeginRow("Maximum X (px)", "Right edge, exclusive.")) {
      gui_->InputDouble("##zone_max_x", &draft->max_point.x, 1.0, 16.0, "%.0f");
    }
    if (grid.BeginRow("Maximum Y (px)", "Bottom edge, exclusive.")) {
      gui_->InputDouble("##zone_max_y", &draft->max_point.y, 1.0, 16.0, "%.0f");
    }
  }

  if (creation_error_) {
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "%s", creation_error_->c_str());
  }
  if (gui_->Button("Create Zone")) {
    absl::StatusOr<int> committed = creation_model_.Commit(level, themes);
    if (!committed.ok()) {
      creation_error_ = committed.status().message();
    } else {
      selection.Clear();
      selection.type = SelectionState::Type::kZone;
      selection.zone_id = *committed;
      creation_error_.reset();
      return *committed;
    }
  }
  gui_->SameLine();
  if (gui_->Button("Cancel")) {
    creation_model_.Cancel();
    creation_error_.reset();
    selection.Clear();
    selection.type = SelectionState::Type::kLevel;
  }
  return std::nullopt;
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

  RenderInspectorSection(*gui_, "IDENTITY", "The name shown in the Level Contents hierarchy.");
  {
    InspectorPropertyGrid grid(*gui_, "ZoneIdentity");
    if (grid.BeginRow("Name", "Display name for this parallax region.")) {
      gui_->InputText("##zone_name", &zone.name);
    }
  }

  const std::vector<ParallaxTheme> themes = api_->GetAllParallaxThemes();
  RenderInspectorSection(*gui_, "BACKGROUND",
                         "Assign a reusable theme or open its standalone editor.");
  {
    InspectorPropertyGrid grid(*gui_, "ZoneTheme");
    RETURN_IF_ERROR(RenderThemePicker(grid, zone.theme_id, themes));
  }

  const bool has_theme = std::any_of(themes.begin(), themes.end(), [&](const ParallaxTheme& theme) {
    return theme.id == zone.theme_id;
  });
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

  RenderInspectorSection(*gui_, "WORLD BOUNDS",
                         "Rectangle in world pixels where this background can become active.");
  {
    InspectorPropertyGrid grid(*gui_, "ZoneBounds");
    if (grid.BeginRow("Minimum X (px)", "Left edge, inclusive.")) {
      gui_->InputDouble("##zone_min_x", &zone.min_point.x, 1.0, 16.0, "%.0f");
    }
    if (grid.BeginRow("Minimum Y (px)", "Top edge, inclusive.")) {
      gui_->InputDouble("##zone_min_y", &zone.min_point.y, 1.0, 16.0, "%.0f");
    }
    if (grid.BeginRow("Maximum X (px)", "Right edge, exclusive.")) {
      gui_->InputDouble("##zone_max_x", &zone.max_point.x, 1.0, 16.0, "%.0f");
    }
    if (grid.BeginRow("Maximum Y (px)", "Bottom edge, exclusive.")) {
      gui_->InputDouble("##zone_max_y", &zone.max_point.y, 1.0, 16.0, "%.0f");
    }
  }

  RenderInspectorSection(*gui_, "TRANSITION", "Reserved for future two-theme blending.");
  gui_->TextWrapped(
      "Zone fades are not rendered yet. Values are preserved but cannot be edited "
      "until the two-theme compositor is implemented.");
  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled();
    InspectorPropertyGrid grid(*gui_, "ZoneTransition");
    if (grid.BeginRow("Fade X (px)", "Not rendered yet.")) {
      gui_->InputDouble("##zone_fade_x", &zone.fade_length.x, 0.0, 0.0, "%.0f");
    }
    if (grid.BeginRow("Fade Y (px)", "Not rendered yet.")) {
      gui_->InputDouble("##zone_fade_y", &zone.fade_length.y, 0.0, 0.0, "%.0f");
    }
  }
  if ((zone.fade_length.x != 0.0 || zone.fade_length.y != 0.0) &&
      gui_->Button("Reset Fades to Zero")) {
    zone.fade_length = {};
  }

  if (gui_->CollapsingHeader("Advanced##ParallaxZone")) {
    InspectorPropertyGrid grid(*gui_, "ZoneAdvanced");
    if (grid.BeginRow("Stable ID", "Integer identity unique within this level.")) {
      gui_->BeginDisabled();
      gui_->InputInt("##zone_id", &zone.id);
      gui_->EndDisabled();
    }
  }

  RenderInspectorSection(*gui_, "DANGER ZONE", "Deleting removes this zone from the level.");
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
