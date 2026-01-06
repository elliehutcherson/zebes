#include "editor/level_editor/parallax_panel.h"

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace zebes {

absl::StatusOr<ParallaxResult> ParallaxPanel::Render(Level& level) {
  ImGui::PushID("ParallaxPanel");
  auto cleanup = absl::MakeCleanup([] { ImGui::PopID(); });

  ImGui::Text("Parallax Layers");
  ImGui::Separator();

  if (editing_layer_.has_value()) {
    return RenderDetails(level);
  }

  return RenderList(level);
}

absl::StatusOr<ParallaxResult> ParallaxPanel::RenderList(Level& level) {
  ParallaxResult result;

  if (ImGui::Button("Create")) {
    editing_layer_ = ParallaxLayer{
        .name = absl::StrCat("Layer ", level.parallax_layers.size()),
        .texture_id = "",
        .scroll_factor = {1.0, 1.0},
    };
    selected_index_ = -1;
  }
  ImGui::SameLine();

  if (ImGui::Button("Edit") && selected_index_ >= 0 &&
      selected_index_ < level.parallax_layers.size()) {
    editing_layer_ = level.parallax_layers[selected_index_];
  }
  ImGui::SameLine();

  {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    auto cleanup = absl::MakeCleanup([] { ImGui::PopStyleColor(); });
    if (ImGui::Button("Delete") && selected_index_ >= 0 &&
        selected_index_ < level.parallax_layers.size()) {
      RETURN_IF_ERROR(ConfirmState(level, Op::kLayerDelete));
      result.type = ParallaxResult::kChanged;
      selected_index_ = -1;
    }
  }

  // List Box
  if (ImGui::BeginListBox("Layers", ImVec2(-FLT_MIN, -FLT_MIN))) {
    for (int i = 0; i < level.parallax_layers.size(); ++i) {
      const bool is_selected = (selected_index_ == i);
      const auto& layer = level.parallax_layers[i];

      if (ImGui::Selectable(layer.name.c_str(), is_selected)) {
        selected_index_ = i;
      }

      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndListBox();
  }

  return result;
}

absl::StatusOr<ParallaxResult> ParallaxPanel::RenderDetails(Level& level) {
  ParallaxResult result;

  ImGui::InputText("Name", &editing_layer_->name);
  ImGui::InputText("Texture ID", &editing_layer_->texture_id);
  ImGui::Checkbox("Repeat X", &editing_layer_->repeat_x);

  ImGui::Text("Scroll Factor");
  ImGui::InputDouble("X", &editing_layer_->scroll_factor.x);
  ImGui::InputDouble("Y", &editing_layer_->scroll_factor.y);

  ImGui::Separator();

  if (ImGui::Button("Save")) {
    // Determine if we are updating existing or creating new
    Op op = (selected_index_ >= 0) ? Op::kLayerUpdate : Op::kLayerCreate;
    RETURN_IF_ERROR(ConfirmState(level, op));
    result.type = ParallaxResult::kChanged;
    editing_layer_.reset();
  }
  ImGui::SameLine();

  if (ImGui::Button("Cancel")) {
    editing_layer_.reset();
  }

  return result;
}

absl::Status ParallaxPanel::ConfirmState(Level& level, Op op) {
  if (op != Op::kLayerDelete && !editing_layer_.has_value()) {
    return absl::InternalError("No editing layer present");
  }

  switch (op) {
    case Op::kLayerCreate:
      level.parallax_layers.push_back(*editing_layer_);
      // Select the new layer
      selected_index_ = level.parallax_layers.size() - 1;
      break;
    case Op::kLayerUpdate:
      if (selected_index_ >= 0 && selected_index_ < level.parallax_layers.size()) {
        level.parallax_layers[selected_index_] = *editing_layer_;
      }
      break;
    case Op::kLayerDelete:
      if (selected_index_ >= 0 && selected_index_ < level.parallax_layers.size()) {
        level.parallax_layers.erase(level.parallax_layers.begin() + selected_index_);
      }
      break;
  }

  return absl::OkStatus();
}

}  // namespace zebes
