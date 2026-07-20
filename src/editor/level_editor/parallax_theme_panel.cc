#include "editor/level_editor/parallax_theme_panel.h"

#include <algorithm>
#include <string>

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

ParallaxThemePanel::ParallaxThemePanel(Options options)
    : api_(*options.api), gui_(options.gui), texture_preview_(*options.gui) {}

absl::Status ParallaxThemePanel::RenderNavigator(Level& level, SelectionState& selection) {
  if (gui_->Button("Add Theme")) {
    AddTheme(level, selection);
  }

  // Iterate over themes
  for (auto& [id, theme] : level.themes) {
    ImGuiTreeNodeFlags theme_flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;

    if (selection.type == SelectionState::Type::kTheme && selection.theme_id == id) {
      theme_flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool theme_open = gui_->CollapsingHeader(theme.name.c_str(), theme_flags);
    if (gui_->IsItemClicked()) {
      selection.type = SelectionState::Type::kTheme;
      selection.theme_id = id;
    }

    if (!theme_open) continue;
    // Indent layers
    gui_->Indent(10.0f);

    // Layers
    for (int i = 0; i < theme.layers.size(); ++i) {
      ParallaxLayer& layer = theme.layers[i];
      bool is_selected = (selection.type == SelectionState::Type::kLayer &&
                          selection.theme_id == id && selection.layer_index == i);

      std::string label = absl::StrCat(layer.name, "##", i);
      if (gui_->Selectable(label.c_str(), is_selected)) {
        selection.type = SelectionState::Type::kLayer;
        selection.theme_id = id;
        selection.layer_index = i;
      }
    }

    gui_->Unindent(10.0f);
  }
  return absl::OkStatus();
}

absl::Status ParallaxThemePanel::RenderThemeDetails(Level& level, SelectionState& selection) {
  auto it = level.themes.find(selection.theme_id);
  if (it == level.themes.end()) {
    selection.Clear();
    return absl::OkStatus();
  }

  ParallaxTheme& theme = it->second;

  gui_->TextDisabled("Theme Properties");
  gui_->Separator();

  gui_->BeginDisabled();
  gui_->InputInt("Id", &theme.id);
  gui_->EndDisabled();

  gui_->InputText("Name", &theme.name);

  gui_->Spacing();
  if (gui_->Button("Add Layer")) {
    ParallaxLayer new_layer{.name = absl::StrCat("Layer ", theme.layers.size()),
                            .scroll_factor = {1.0, 1.0}};
    theme.layers.push_back(new_layer);

    // Auto-select new layer
    selection.type = SelectionState::Type::kLayer;
    selection.layer_index = theme.layers.size() - 1;
  }

  gui_->SameLine();
  {
    ScopedStyleColor color =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete Theme")) {
      level.themes.erase(selection.theme_id);
      selection.Clear();
    }
  }

  return absl::OkStatus();
}

absl::Status ParallaxThemePanel::RenderLayerDetails(Level& level, SelectionState& selection) {
  auto it = level.themes.find(selection.theme_id);
  if (it == level.themes.end()) {
    selection.Clear();
    return absl::OkStatus();
  }
  ParallaxTheme& theme = it->second;

  if (selection.layer_index < 0 || selection.layer_index >= theme.layers.size()) {
    selection.type = SelectionState::Type::kTheme;  // Fallback
    return absl::OkStatus();
  }

  ParallaxLayer& layer = theme.layers[selection.layer_index];

  gui_->TextDisabled("Layer Properties");
  gui_->Separator();

  gui_->InputText("Name", &layer.name);
  gui_->InputText("Texture ID", &layer.texture_id, ImGuiInputTextFlags_ReadOnly);

  gui_->Checkbox("Repeat X", &layer.repeat_x);
  gui_->Checkbox("Repeat Y", &layer.repeat_y);

  gui_->Text("Scroll Factor");
  gui_->InputDouble("X", &layer.scroll_factor.x);
  gui_->InputDouble("Y", &layer.scroll_factor.y);
  gui_->InputFloat("Scale", &layer.base_scale);

  gui_->Separator();
  gui_->Text("Texture Selector");

  // Texture Combo
  if (auto combo = gui_->CreateScopedCombo("Texture", layer.texture_id.c_str()); combo) {
    for (const Texture& tex : texture_cache_) {
      bool is_selected = (layer.texture_id == tex.id);
      if (gui_->Selectable(tex.name_id().c_str(), is_selected)) {
        layer.texture_id = tex.id;
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  gui_->Separator();
  gui_->Text("Source Texture");
  if (layer.texture_id.empty()) {
    gui_->TextDisabled("No texture selected.");
  } else {
    auto texture = std::find_if(texture_cache_.begin(), texture_cache_.end(),
                                [&](const Texture& candidate) {
                                  return candidate.id == layer.texture_id;
                                });
    if (texture == texture_cache_.end() || !texture->texture_handle) {
      return absl::FailedPreconditionError("selected layer texture is unavailable");
    }

    constexpr float kMaximumPreviewWidth = 240.0f;
    constexpr float kMaximumPreviewHeight = 140.0f;
    const float preview_width =
        std::min(kMaximumPreviewWidth, gui_->GetContentRegionAvail().x);
    if (preview_width > 0.0f) {
      ASSIGN_OR_RETURN(TexturePreviewLayout preview,
                       texture_preview_.Render(texture->texture_handle, preview_width,
                                               kMaximumPreviewHeight));
      gui_->Text("Source Size: %dx%d", preview.source_width, preview.source_height);
    }
  }

  gui_->Spacing();
  {
    ScopedStyleColor color =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete Layer")) {
      theme.layers.erase(theme.layers.begin() + selection.layer_index);
      selection.type = SelectionState::Type::kTheme;  // Select parent
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

void ParallaxThemePanel::AddTheme(Level& level, SelectionState& selection) {
  std::string new_name = absl::StrCat("Theme ", level.themes.size());
  int new_id = 0;
  for (const auto& [id, theme] : level.themes) {
    new_id = std::max(new_id, id + 1);
  }
  level.themes[new_id] = ParallaxTheme{
      .id = new_id,
      .name = new_name,
  };

  selection.type = SelectionState::Type::kTheme;
  selection.theme_id = new_id;
}

}  // namespace zebes
