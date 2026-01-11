#include "editor/level_editor/parallax_panel.h"

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/api.h"
#include "common/status_macros.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "objects/level.h"
#include "objects/texture.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<ParallaxPanel>> ParallaxPanel::Create(Options options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("API can not be null.");
  }

  auto ret = absl::WrapUnique(new ParallaxPanel(std::move(options)));
  RETURN_IF_ERROR(ret->RefreshTextureCache());
  return ret;
}

ParallaxPanel::ParallaxPanel(Options options) : api_(*options.api) {}

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
    ASSIGN_OR_RETURN(result, HandleOp(level, Op::kLayerCreate));
  }
  ImGui::SameLine();

  if (ImGui::Button("Edit")) {
    ASSIGN_OR_RETURN(result, HandleOp(level, Op::kLayerEdit));
  }
  ImGui::SameLine();

  {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    auto cleanup = absl::MakeCleanup([] { ImGui::PopStyleColor(); });
    if (ImGui::Button("Delete")) {
      ASSIGN_OR_RETURN(result, HandleOp(level, Op::kLayerDelete));
    }
  }

  // List Box
  if (ImGui::BeginListBox("Layers", ImVec2(-FLT_MIN, -FLT_MIN))) {
    for (int i = 0; i < level.parallax_layers.size(); ++i) {
      const bool is_selected = (selected_index_ == i);
      const ParallaxLayer& layer = level.parallax_layers[i];

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

  // Texture Selection
  if (ImGui::BeginListBox("Texture ID")) {
    for (const Texture& texture : texture_cache_) {
      bool is_selected = (editing_layer_->texture_id == texture.id);
      if (ImGui::Selectable(texture.name_id().c_str(), is_selected)) {
        editing_layer_->texture_id = texture.id;
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndListBox();
  }

  ImGui::Checkbox("Repeat X", &editing_layer_->repeat_x);

  ImGui::Text("Scroll Factor");
  ImGui::InputDouble("X", &editing_layer_->scroll_factor.x);
  ImGui::InputDouble("Y", &editing_layer_->scroll_factor.y);

  ImGui::Separator();

  if (ImGui::Button("Save")) {
    // Determine if we are updating existing or creating new
    Op op = Op::kLayerUpdate;
    ASSIGN_OR_RETURN(result, HandleOp(level, op));
  }
  ImGui::SameLine();

  if (ImGui::Button("Cancel")) {
    editing_layer_.reset();
  }

  return result;
}
absl::StatusOr<ParallaxResult> ParallaxPanel::HandleOp(Level& level, Op op) {
  ParallaxResult result;
  if (op != Op::kLayerCreate && selected_index_ < 0) {
    return result;
  }

  if (op != Op::kLayerCreate && selected_index_ > level.parallax_layers.size()) {
    return absl::InternalError("Selected index is out of range!");
  }

  switch (op) {
    case Op::kLayerCreate:
      editing_layer_ = ParallaxLayer{
          .name = absl::StrCat("Layer ", level.parallax_layers.size()),
          .texture_id = "",
          .scroll_factor = {1.0, 1.0},
      };

      level.parallax_layers.push_back(*editing_layer_);
      // Select the new layer
      selected_index_ = level.parallax_layers.size() - 1;
      // Enter edit mode immediately
      result.type = ParallaxResult::kChanged;
      break;
    case Op::kLayerEdit:
      editing_layer_ = level.parallax_layers[selected_index_];
      break;
    case Op::kLayerUpdate:
      if (editing_layer_->name.empty()) {
        return absl::InvalidArgumentError("Layer name cannot be empty");
      }
      if (editing_layer_->texture_id.empty()) {
        return absl::InvalidArgumentError("Layer texture must be selected");
      }
      level.parallax_layers[selected_index_] = *editing_layer_;
      result.type = ParallaxResult::kChanged;
      editing_layer_.reset();
      break;
    case Op::kLayerDelete:
      level.parallax_layers.erase(level.parallax_layers.begin() + selected_index_);
      selected_index_ = -1;
      result.type = ParallaxResult::kChanged;
      break;
  }

  return result;
}

absl::Status ParallaxPanel::RefreshTextureCache() {
  ASSIGN_OR_RETURN(texture_cache_, api_.GetAllTextures());

  std::sort(texture_cache_.begin(), texture_cache_.end(),
            [](const Texture& a, const Texture& b) { return a.name < b.name; });

  LOG(INFO) << "Loaded " << texture_cache_.size() << " textures.";
  return absl::OkStatus();
}

}  // namespace zebes
