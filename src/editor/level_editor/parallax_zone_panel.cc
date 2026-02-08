#include "editor/level_editor/parallax_zone_panel.h"

#include "absl/memory/memory.h"
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

absl::StatusOr<ParallaxZoneResult> ParallaxZonePanel::Render(Level& level) {
  ScopedId scoped_id = gui_->CreateScopedId("ParallaxZonePanel");

  if (!error_.empty()) {
    ScopedStyleColor color =
        gui_->CreateScopedStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    gui_->TextWrapped("%s", error_.c_str());
    gui_->Separator();
  }

  switch (state_) {
    case State::kZoneList:
      gui_->Text("Parallax Zones");
      gui_->Separator();
      RETURN_IF_ERROR(RenderZoneList(level));
      break;
    case State::kZoneDetails:
      gui_->Text("Edit Zone");
      gui_->Separator();
      RETURN_IF_ERROR(RenderZoneDetails(level));
      return ParallaxZoneResult::kEdit;
  }

  return ParallaxZoneResult::kNone;
}

absl::Status ParallaxZonePanel::RenderZoneList(Level& level) {
  const float button_width =
      (gui_->GetContentRegionAvail().x - gui_->GetStyle().ItemSpacing.x * 2) / 3.0f;

  if (gui_->Button("New Zone", ImVec2(button_width, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kCreateZone));
  }
  gui_->SameLine();

  if (gui_->Button("Edit Zone", ImVec2(button_width, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kEditZone));
  }
  gui_->SameLine();

  {
    ScopedStyleColor color =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete Zone", ImVec2(button_width, 0))) {
      RETURN_IF_ERROR(HandleOp(level, Op::kDeleteZone));
    }
  }

  if (auto listbox = ScopedListBox(gui_, "##Zones", ImVec2(-FLT_MIN, -FLT_MIN)); listbox) {
    for (int i = 0; i < level.zones.size(); ++i) {
      std::string name = absl::StrCat("Zone ", i);
      bool is_selected = (selected_zone_index_ == i);
      if (gui_->Selectable(name.c_str(), is_selected)) {
        selected_zone_index_ = i;
      }
      if (is_selected) {
        gui_->SetItemDefaultFocus();
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ParallaxZonePanel::RenderZoneDetails(Level& level) {
  if (gui_->Button("Back", ImVec2(-FLT_MIN, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kBackToZones));
    return absl::OkStatus();
  }

  if (!editing_zone_.has_value()) return absl::InternalError("No editing zone");

  // Theme support
  if (gui_->BeginCombo("Theme", editing_zone_->theme_id.c_str())) {
    for (const auto& [name, theme] : level.themes) {
      bool is_selected = (editing_zone_->theme_id == name);
      if (gui_->Selectable(name.c_str(), is_selected)) {
        editing_zone_->theme_id = name;
      }
      if (is_selected) {
        gui_->SetItemDefaultFocus();
      }
    }
    gui_->EndCombo();
  }

  gui_->Text("Boundaries");
  gui_->InputDouble("Min X", &editing_zone_->min_point.x);
  gui_->InputDouble("Min Y", &editing_zone_->min_point.y);
  gui_->InputDouble("Max X", &editing_zone_->max_point.x);
  gui_->InputDouble("Max Y", &editing_zone_->max_point.y);

  gui_->Text("Fade Length");
  gui_->InputDouble("Fade X", &editing_zone_->fade_length.x);
  gui_->InputDouble("Fade Y", &editing_zone_->fade_length.y);

  const float button_width =
      (gui_->GetContentRegionAvail().x - gui_->GetStyle().ItemSpacing.x * 2) / 3.0f;

  if (gui_->Button("Save", ImVec2(-FLT_MIN, 0))) {
    absl::Status save_result = HandleOp(level, Op::kSaveZone);
    if (save_result.code() == absl::StatusCode::kInvalidArgument) {
      error_ = save_result.message();
    } else if (!save_result.ok()) {
      return save_result;
    }
  }

  return absl::OkStatus();
}

absl::Status ParallaxZonePanel::HandleOp(Level& level, Op op) {
  error_.clear();

  switch (op) {
    case Op::kCreateZone: {
      ParallaxZone new_zone;
      // Default somewhat reasonable values?
      new_zone.min_point = {0, 0};
      new_zone.max_point = {100, 100};
      new_zone.fade_length = {10, 10};
      level.zones.push_back(new_zone);
      selected_zone_index_ = level.zones.size() - 1;
      break;
    }
    case Op::kDeleteZone:
      if (selected_zone_index_ >= 0 && selected_zone_index_ < level.zones.size()) {
        level.zones.erase(level.zones.begin() + selected_zone_index_);
        selected_zone_index_ = -1;
      }
      break;
    case Op::kEditZone:
      if (selected_zone_index_ < 0 || selected_zone_index_ >= level.zones.size()) {
        return absl::InternalError("Invalid zone index");
      }
      editing_zone_ = level.zones[selected_zone_index_];
      state_ = State::kZoneDetails;
      break;

    case Op::kBackToZones:
      state_ = State::kZoneList;
      editing_zone_.reset();
      break;

    case Op::kSaveZone:
      if (!editing_zone_.has_value()) return absl::InternalError("No editing zone to save");
      level.zones[selected_zone_index_] = *editing_zone_;
      break;
  }

  return absl::OkStatus();
}

}  // namespace zebes
