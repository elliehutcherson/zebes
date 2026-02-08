#include "editor/level_editor/parallax_theme_panel.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<ParallaxThemePanel>> ParallaxThemePanel::Create(Options options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("API can not be null.");
  }
  if (options.gui == nullptr) {
    return absl::InvalidArgumentError("Gui can not be null.");
  }

  auto ret = absl::WrapUnique(new ParallaxThemePanel(std::move(options)));
  RETURN_IF_ERROR(ret->RefreshTextureCache());
  return ret;
}

ParallaxThemePanel::ParallaxThemePanel(Options options) : api_(*options.api), gui_(options.gui) {}

absl::StatusOr<ParallaxThemeResult> ParallaxThemePanel::Render(Level& level) {
  ScopedId scoped_id = gui_->CreateScopedId("ParallaxThemePanel");

  if (!error_.empty()) {
    ScopedStyleColor color =
        gui_->CreateScopedStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    gui_->TextWrapped("%s", error_.c_str());
    gui_->Separator();
  }

  switch (state_) {
    case State::kThemeList:
      gui_->Text("Parallax Themes");
      gui_->Separator();
      RETURN_IF_ERROR(RenderThemeList(level));
      break;
    case State::kLayerList:
      gui_->SetNextItemWidth(200);
      if (gui_->InputText("##ThemeName", &editing_theme_name_,
                          ImGuiInputTextFlags_EnterReturnsTrue)) {
        RETURN_IF_ERROR(HandleOp(level, Op::kRenameTheme));
      }
      if (gui_->IsItemDeactivatedAfterEdit()) {
        RETURN_IF_ERROR(HandleOp(level, Op::kRenameTheme));
      }
      gui_->SameLine();
      gui_->Text("(Edit Name)");
      gui_->Separator();
      RETURN_IF_ERROR(RenderLayerList(level));
      break;
    case State::kLayerDetails:
      gui_->Text("Edit Layer");
      gui_->Separator();
      RETURN_IF_ERROR(RenderLayerDetails(level));
      return ParallaxThemeResult::kEdit;
  }

  return ParallaxThemeResult::kNone;
}

absl::Status ParallaxThemePanel::RenderThemeList(Level& level) {
  const float button_width =
      (gui_->GetContentRegionAvail().x - gui_->GetStyle().ItemSpacing.x * 2) / 3.0f;

  if (gui_->Button("New Theme", ImVec2(button_width, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kCreateTheme));
  }

  gui_->SameLine();

  if (gui_->Button("Edit Theme", ImVec2(button_width, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kEditTheme));
  }

  gui_->SameLine();

  {
    ScopedStyleColor color =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete Theme", ImVec2(button_width, 0))) {
      RETURN_IF_ERROR(HandleOp(level, Op::kDeleteTheme));
    }
  }

  if (auto listbox = ScopedListBox(gui_, "##Themes", ImVec2(-FLT_MIN, -FLT_MIN)); listbox) {
    for (auto& [name, theme] : level.themes) {
      bool is_selected = (selected_theme_name_ == name);
      if (gui_->Selectable(name.c_str(), is_selected)) {
        selected_theme_name_ = name;
        // Selection only updates the tracker, explicit "Edit" button needed to proceed.
      }
      if (is_selected) {
        gui_->SetItemDefaultFocus();
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ParallaxThemePanel::RenderLayerList(Level& level) {
  const float button_width =
      (gui_->GetContentRegionAvail().x - gui_->GetStyle().ItemSpacing.x * 3) / 4.0f;

  if (gui_->Button("Back", ImVec2(button_width, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kBackToThemes));
  }
  gui_->SameLine();

  if (gui_->Button("Add Layer", ImVec2(button_width, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kCreateLayer));
  }
  gui_->SameLine();

  if (gui_->Button("Edit Layer", ImVec2(button_width, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kEditLayer));
  }
  gui_->SameLine();

  {
    ScopedStyleColor color =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete Layer", ImVec2(button_width, 0))) {
      RETURN_IF_ERROR(HandleOp(level, Op::kDeleteLayer));
    }
  }

  auto it = level.themes.find(selected_theme_name_);
  if (it == level.themes.end()) {
    // Should not happen, but safe fallback
    state_ = State::kThemeList;
    return absl::OkStatus();
  }

  ParallaxTheme& theme = it->second;

  if (auto listbox = ScopedListBox(gui_, "##Layers", ImVec2(-FLT_MIN, -FLT_MIN)); listbox) {
    for (int i = 0; i < theme.layers.size(); ++i) {
      const ParallaxLayer& layer = theme.layers[i];
      bool is_selected = (selected_layer_index_ == i);
      if (gui_->Selectable(layer.name.c_str(), is_selected)) {
        selected_layer_index_ = i;
        // Selection only updates the tracker, explicit "Edit" button needed to proceed.
      }
      if (is_selected) {
        gui_->SetItemDefaultFocus();
      }
    }
  }

  return absl::OkStatus();
}

absl::Status ParallaxThemePanel::RenderLayerDetails(Level& level) {
  if (gui_->Button("Back", ImVec2(-FLT_MIN, 0))) {
    RETURN_IF_ERROR(HandleOp(level, Op::kBackToLayers));
    return absl::OkStatus();
  }

  if (!editing_layer_.has_value()) return absl::InternalError("No editing layer");

  gui_->InputText("Name", &editing_layer_->name);

  // Read-only texture id
  std::string texture_id_copy = editing_layer_->texture_id;
  gui_->InputText("Texture ID", &texture_id_copy, ImGuiInputTextFlags_ReadOnly);

  gui_->Checkbox("Repeat X", &editing_layer_->repeat_x);
  gui_->Checkbox("Repeat Y",
                 &editing_layer_->repeat_y);  // Added Repeat Y support as it is in the struct

  gui_->Text("Scroll Factor");
  gui_->InputDouble("X", &editing_layer_->scroll_factor.x);
  gui_->InputDouble("Y", &editing_layer_->scroll_factor.y);

  gui_->Text("Scale");
  gui_->InputFloat("Base Scale", &editing_layer_->base_scale);

  // Texture Selection
  gui_->Text("Texture Cache");
  if (auto list_box = ScopedListBox(gui_, "##TextureCache", ImVec2(-FLT_MIN, 150)); list_box) {
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
    RETURN_IF_ERROR(HandleOp(level, Op::kChangeLayerTexture));
  }

  gui_->SameLine();

  if (gui_->Button("Save", ImVec2(button_width, 0))) {
    absl::Status save_result = HandleOp(level, Op::kSaveLayer);
    if (save_result.code() == absl::StatusCode::kInvalidArgument) {
      error_ = save_result.message();
    } else if (!save_result.ok()) {
      return save_result;
    }
  }

  return absl::OkStatus();
}

absl::Status ParallaxThemePanel::HandleOp(Level& level, Op op) {
  error_.clear();

  switch (op) {
    case Op::kCreateTheme: {
      std::string new_name = absl::StrCat("Theme ", level.themes.size());
      if (level.themes.contains(new_name)) {
        // Simple collision resolution
        new_name = absl::StrCat(new_name, "_", rand() % 1000);
      }
      level.themes[new_name] = ParallaxTheme{.name = new_name};
      selected_theme_name_ = new_name;
      // Don't auto-switch, user might want to create multiple
      break;
    }
    case Op::kDeleteTheme:
      if (!selected_theme_name_.empty()) {
        level.themes.erase(selected_theme_name_);
        selected_theme_name_.clear();
      }
      break;
    case Op::kEditTheme:
      state_ = State::kLayerList;
      selected_layer_index_ = -1;
      editing_theme_name_ = selected_theme_name_;
      break;

    case Op::kRenameTheme:
      if (editing_theme_name_.empty()) return absl::InvalidArgumentError("Name cannot be empty");
      if (editing_theme_name_ == selected_theme_name_) return absl::OkStatus();  // No change

      if (level.themes.contains(editing_theme_name_)) {
        return absl::AlreadyExistsError("Theme name already exists");
      }

      {
        // Move the node
        auto node = level.themes.extract(selected_theme_name_);
        node.key() = editing_theme_name_;
        node.mapped().name = editing_theme_name_;
        level.themes.insert(std::move(node));
        selected_theme_name_ = editing_theme_name_;
      }
      break;

    case Op::kBackToThemes:
      state_ = State::kThemeList;
      selected_theme_name_.clear();
      break;

    case Op::kCreateLayer: {
      auto it = level.themes.find(selected_theme_name_);
      if (it == level.themes.end()) return absl::InternalError("Theme not found");

      auto& theme = it->second;
      ParallaxLayer new_layer{.name = absl::StrCat("Layer ", theme.layers.size()),
                              .texture_id = "",
                              .scroll_factor = {1.0, 1.0}};
      theme.layers.push_back(new_layer);
      selected_layer_index_ = theme.layers.size() - 1;

      // Auto-enter edit mode for new layer? Or just select it.
      // Let's just select it for list view.
      break;
    }

    case Op::kDeleteLayer: {
      auto it = level.themes.find(selected_theme_name_);
      if (it == level.themes.end()) return absl::InternalError("Theme not found");
      auto& theme = it->second;

      if (selected_layer_index_ >= 0 && selected_layer_index_ < theme.layers.size()) {
        theme.layers.erase(theme.layers.begin() + selected_layer_index_);
        selected_layer_index_ = -1;
      }
      break;
    }

    case Op::kEditLayer: {
      auto it = level.themes.find(selected_theme_name_);
      if (it == level.themes.end()) return absl::InternalError("Theme not found");
      auto& theme = it->second;

      if (selected_layer_index_ < 0 || selected_layer_index_ >= theme.layers.size()) {
        return absl::InternalError("Invalid layer index");
      }

      editing_layer_ = theme.layers[selected_layer_index_];
      state_ = State::kLayerDetails;

      // Setup texture selection
      selected_texture_index_ = -1;
      for (int i = 0; i < texture_cache_.size(); ++i) {
        if (texture_cache_[i].id == editing_layer_->texture_id) {
          selected_texture_index_ = i;
          break;
        }
      }
      break;
    }

    case Op::kBackToLayers:
      state_ = State::kLayerList;
      editing_layer_.reset();
      break;

    case Op::kChangeLayerTexture:
      if (selected_texture_index_ >= 0 && selected_texture_index_ < texture_cache_.size()) {
        editing_layer_->texture_id = texture_cache_[selected_texture_index_].id;
      }
      break;

    case Op::kSaveLayer: {
      if (editing_layer_->name.empty()) {
        return absl::InvalidArgumentError("Layer name cannot be empty");
      }
      // Note: we might allow empty texture if it's just a color layer or something, but following
      // existing logic
      if (editing_layer_->texture_id.empty()) {
        return absl::InvalidArgumentError("Layer texture must be selected");
      }

      auto it = level.themes.find(selected_theme_name_);
      if (it == level.themes.end()) return absl::InternalError("Theme not found during save");
      auto& theme = it->second;

      if (selected_layer_index_ < 0 || selected_layer_index_ >= theme.layers.size()) {
        return absl::InternalError("Layer index invalid during save");
      }

      theme.layers[selected_layer_index_] = *editing_layer_;
      break;
    }
  }

  return absl::OkStatus();
}

absl::Status ParallaxThemePanel::RefreshTextureCache() {
  ASSIGN_OR_RETURN(texture_cache_, api_.GetAllTextures());

  std::sort(texture_cache_.begin(), texture_cache_.end(),
            [](const Texture& a, const Texture& b) { return a.name < b.name; });

  return absl::OkStatus();
}

std::optional<std::string> ParallaxThemePanel::GetTexture() const {
  if (state_ != State::kLayerDetails || !editing_layer_.has_value()) {
    return std::nullopt;
  }
  // If we have a selected texture in the list, preview that?
  if (selected_texture_index_ >= 0 && selected_texture_index_ < texture_cache_.size()) {
    return texture_cache_[selected_texture_index_].id;
  }
  return editing_layer_->texture_id;
}

}  // namespace zebes
