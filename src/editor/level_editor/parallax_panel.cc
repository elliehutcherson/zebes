#include "editor/level_editor/parallax_panel.h"

#include <optional>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/api.h"
#include "common/status_macros.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"
#include "objects/level.h"
#include "objects/texture.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<ParallaxPanel>> ParallaxPanel::Create(Options options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("API can not be null.");
  }
  if (options.gui == nullptr) {
    return absl::InvalidArgumentError("Gui can not be null.");
  }

  auto ret = absl::WrapUnique(new ParallaxPanel(std::move(options)));
  RETURN_IF_ERROR(ret->RefreshTextureCache());
  return ret;
}

ParallaxPanel::ParallaxPanel(Options options) : api_(*options.api), gui_(options.gui) {}

void ParallaxPanel::Reset() {
  selected_index_ = -1;
  selected_texture_index_ = -1;
  editing_layer_.reset();
  error_.clear();
}

absl::StatusOr<ParallaxResult> ParallaxPanel::Render(Level& level) {
  {
    ScopedId scoped_id(gui_, "ParallaxPanel");
    gui_->Text("Parallax Layers");
    gui_->Separator();

    if (editing_layer_.has_value()) {
      RETURN_IF_ERROR(RenderDetails(level));
    } else {
      RETURN_IF_ERROR(RenderList(level));
    }

    if (editing_layer_.has_value()) {
      return ParallaxResult::kEdit;
    }
  }
  return ParallaxResult::kList;
}

absl::Status ParallaxPanel::RenderList(Level& level) {
  const float button_width =
      (gui_->GetContentRegionAvail().x - gui_->GetStyle().ItemSpacing.x * 2) / 3.0f;
  if (gui_->Button("Create", ImVec2(button_width, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kParallaxCreate));
  }
  gui_->SameLine();

  if (gui_->Button("Edit", ImVec2(button_width, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kParallaxEdit));
  }
  gui_->SameLine();

  {
    ScopedStyleColor color(gui_, ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete", ImVec2(button_width, 0))) {
      RETURN_IF_ERROR(HandleOp(level, Op::kParallaxDelete));
    }
  }

  // List Box
  if (auto listbox = ScopedListBox(gui_, "##Layers", ImVec2(-FLT_MIN, -FLT_MIN)); listbox) {
    for (int i = 0; i < level.parallax_layers.size(); ++i) {
      const bool is_selected = (selected_index_ == i);
      const ParallaxLayer& layer = level.parallax_layers[i];

      if (gui_->Selectable(layer.name.c_str(), is_selected)) {
        selected_index_ = i;
      }

      if (is_selected) {
        gui_->SetItemDefaultFocus();
      }
    }
  }

  return absl::OkStatus();
}

absl::Status ParallaxPanel::RenderDetails(Level& level) {
  if (gui_->Button("Back", ImVec2(-FLT_MIN, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kParallaxBack));
  }

  gui_->InputText("Name", &editing_layer_->name);

  // Read-only texture id
  std::string texture_id_copy = editing_layer_->texture_id;
  gui_->InputText("Texture ID", &texture_id_copy, ImGuiInputTextFlags_ReadOnly);

  gui_->Checkbox("Repeat X", &editing_layer_->repeat_x);

  gui_->Text("Scroll Factor");
  gui_->InputDouble("X", &editing_layer_->scroll_factor.x);
  gui_->InputDouble("Y", &editing_layer_->scroll_factor.y);

  // Texture Selection
  gui_->Text("Texture Cache");
  if (auto list_box = ScopedListBox(gui_, "##TextureCache", ImVec2(-FLT_MIN, 0)); list_box) {
    for (int i = 0; i < texture_cache_.size(); ++i) {
      const Texture& texture = texture_cache_[i];
      bool is_selected = (selected_texture_index_ == i);
      if (gui_->Selectable(texture.name_id().c_str(), is_selected)) {
        selected_texture_index_ = i;
      }
      if (is_selected) {
        gui_->SetItemDefaultFocus();
      }
    }
  }

  const float button_width =
      (gui_->GetContentRegionAvail().x - gui_->GetStyle().ItemSpacing.x) / 2.0f;
  if (gui_->Button("Change Texture", ImVec2(button_width, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kParallaxTexture));
  }

  gui_->SameLine();

  if (gui_->Button("Save", ImVec2(button_width, 0))) {
    absl::Status save_result = HandleOp(level, Op::kParallaxSave);
    if (save_result.code() == absl::StatusCode::kInvalidArgument) {
      error_ = save_result.message();
    } else if (!save_result.ok()) {
      return save_result;
    }
  }

  if (!error_.empty()) {
    ScopedStyleColor color(gui_, ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    gui_->TextWrapped("%s", error_.c_str());
  }

  return absl::OkStatus();
}

absl::Status ParallaxPanel::HandleOp(Level& level, Op op) {
  // Always clear the error on any new action.
  error_.clear();
  if ((op != Op::kParallaxCreate && op != Op::kParallaxTexture) && selected_index_ < 0) {
    return absl::OkStatus();
  }

  if (op != Op::kParallaxCreate && selected_index_ > level.parallax_layers.size()) {
    return absl::InternalError("Selected index is out of range!");
  }

  switch (op) {
    case Op::kParallaxCreate:
      editing_layer_ = ParallaxLayer{
          .name = absl::StrCat("Layer ", level.parallax_layers.size()),
          .texture_id = "",
          .scroll_factor = {1.0, 1.0},
      };

      level.parallax_layers.push_back(*editing_layer_);
      // Select the new layer
      selected_index_ = level.parallax_layers.size() - 1;

      // Initialize texture selection index
      selected_texture_index_ = -1;
      break;
    case Op::kParallaxEdit:
      editing_layer_ = level.parallax_layers[selected_index_];
      // Initialize texture selection index based on current layer's texture
      selected_texture_index_ = -1;
      for (int i = 0; i < texture_cache_.size(); ++i) {
        if (texture_cache_[i].id == editing_layer_->texture_id) {
          selected_texture_index_ = i;
          break;
        }
      }
      break;
    case Op::kParallaxSave:
      if (editing_layer_->name.empty()) {
        return absl::InvalidArgumentError("Layer name cannot be empty");
      }
      if (editing_layer_->texture_id.empty()) {
        return absl::InvalidArgumentError("Layer texture must be selected");
      }
      level.parallax_layers[selected_index_] = *editing_layer_;
      break;
    case Op::kParallaxDelete:
      level.parallax_layers.erase(level.parallax_layers.begin() + selected_index_);
      selected_index_ = -1;
      break;
    case Op::kParallaxBack:
      ++counters_.back;
      editing_layer_.reset();
      break;
    case Op::kParallaxTexture:
      ++counters_.texture;
      editing_layer_->texture_id = texture_cache_[selected_texture_index_].id;
  }

  return absl::OkStatus();
}

absl::Status ParallaxPanel::RefreshTextureCache() {
  ASSIGN_OR_RETURN(texture_cache_, api_.GetAllTextures());

  std::sort(texture_cache_.begin(), texture_cache_.end(),
            [](const Texture& a, const Texture& b) { return a.name < b.name; });

  LOG(INFO) << "Loaded " << texture_cache_.size() << " textures.";
  return absl::OkStatus();
}

std::optional<std::string> ParallaxPanel::GetTexture() const {
  if (!editing_layer_.has_value()) {
    LOG(ERROR) << __func__ << " should not be called when not editting";
    return std::nullopt;
  }
  if (selected_texture_index_ >= texture_cache_.size()) {
    LOG(ERROR) << "Texture size is invalid!";
    return std::nullopt;
  }
  if (selected_texture_index_ < 0 && editing_layer_->texture_id.empty()) {
    return std::nullopt;
  }
  if (selected_texture_index_ >= 0) {
    return texture_cache_[selected_texture_index_].id;
  }
  return editing_layer_->texture_id;
}

}  // namespace zebes
