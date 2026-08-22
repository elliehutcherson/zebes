#include "editor/parallax_theme_editor/parallax_theme_editor.h"

#include <algorithm>
#include <cfloat>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<ParallaxThemeEditor>> ParallaxThemeEditor::Create(
    Api* api, GuiInterface* gui) {
  if (api == nullptr) return absl::InvalidArgumentError("Api must not be null.");
  if (gui == nullptr) return absl::InvalidArgumentError("Gui must not be null.");
  return absl::WrapUnique(new ParallaxThemeEditor(api, gui));
}

ParallaxThemeEditor::ParallaxThemeEditor(Api* api, GuiInterface* gui)
    : api_(api), gui_(gui), texture_preview_(*gui) {}

absl::Status ParallaxThemeEditor::OpenTheme(const std::string& theme_id) {
  ASSIGN_OR_RETURN(ParallaxTheme * theme, api_->GetParallaxTheme(theme_id));
  if (theme == nullptr) return absl::FailedPreconditionError("Theme lookup returned null.");
  model_.Open(*theme);
  error_.reset();
  return absl::OkStatus();
}

void ParallaxThemeEditor::SetError(const absl::Status& status) {
  if (!status.ok()) error_ = status.message();
}

absl::Status ParallaxThemeEditor::Save() {
  ASSIGN_OR_RETURN(ParallaxTheme request, model_.BuildSaveRequest());
  if (request.id.empty()) {
    ASSIGN_OR_RETURN(const std::string id, api_->CreateParallaxTheme(std::move(request)));
    model_.FinishSave(id);
  } else {
    const std::string id = request.id;
    RETURN_IF_ERROR(api_->UpdateParallaxTheme(std::move(request)));
    model_.FinishSave(id);
  }
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditor::Duplicate() {
  const ParallaxTheme* draft = model_.draft();
  if (draft == nullptr || draft->id.empty()) {
    return absl::FailedPreconditionError("Save the theme before duplicating it.");
  }
  ParallaxTheme duplicate = *draft;
  duplicate.id.clear();
  duplicate.name = absl::StrCat(duplicate.name, " Copy");
  ASSIGN_OR_RETURN(const std::string id, api_->CreateParallaxTheme(std::move(duplicate)));
  return OpenTheme(id);
}

absl::Status ParallaxThemeEditor::Render() {
  if (error_) {
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Error: %s", error_->c_str());
    gui_->SameLine();
    if (gui_->Button("Dismiss")) error_.reset();
  }

  ScopedTable table = gui_->CreateScopedTable("ParallaxThemeEditor", 3,
                                              ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders);
  if (!table) return absl::OkStatus();
  gui_->TableSetupColumn("Themes");
  gui_->TableSetupColumn("Layers");
  gui_->TableSetupColumn("Inspector");
  gui_->TableNextRow();
  gui_->TableNextColumn();

  if (gui_->Button("New Theme")) model_.BeginNew();
  for (const ParallaxTheme& theme : api_->GetAllParallaxThemes()) {
    const bool selected = model_.draft() != nullptr && model_.draft()->id == theme.id;
    if (gui_->Selectable(theme.name.c_str(), selected)) RETURN_IF_ERROR(OpenTheme(theme.id));
  }

  gui_->TableNextColumn();
  ParallaxTheme* draft = model_.draft();
  if (draft == nullptr) {
    gui_->TextDisabled("Select or create a theme.");
    return absl::OkStatus();
  }
  gui_->InputText("Theme Name", &draft->name);
  if (gui_->Button("Add Layer")) RETURN_IF_ERROR(model_.AddLayer());
  for (int index = 0; index < static_cast<int>(draft->layers.size()); ++index) {
    if (gui_->Selectable(draft->layers[index].name.c_str(), model_.selected_layer() == index)) {
      model_.SelectLayer(index);
    }
  }

  gui_->TableNextColumn();
  if (model_.selected_layer()) {
    ParallaxLayer& layer = draft->layers[*model_.selected_layer()];
    gui_->InputText("Layer Name", &layer.name);
    ASSIGN_OR_RETURN(const std::vector<Texture> textures, api_->GetAllTextures());
    const char* preview = layer.texture_id.c_str();
    if (ScopedCombo combo = gui_->CreateScopedCombo("Texture", preview); combo) {
      for (const Texture& texture : textures) {
        const bool selected = texture.id == layer.texture_id;
        if (gui_->Selectable(texture.name_id().c_str(), selected)) layer.texture_id = texture.id;
      }
    }
    gui_->Checkbox("Repeat X", &layer.repeat_x);
    gui_->Checkbox("Repeat Y", &layer.repeat_y);
    gui_->InputDouble("Scroll X", &layer.scroll_factor.x);
    gui_->InputDouble("Scroll Y", &layer.scroll_factor.y);
    gui_->InputDouble("Offset X", &layer.offset.x);
    gui_->InputDouble("Offset Y", &layer.offset.y);
    gui_->InputFloat("Scale", &layer.base_scale);
    if (gui_->Button("Move Farther")) SetError(model_.MoveSelectedLayer(-1));
    gui_->SameLine();
    if (gui_->Button("Move Nearer")) SetError(model_.MoveSelectedLayer(1));
    if (!layer.texture_id.empty()) {
      ASSIGN_OR_RETURN(TextureHandle handle, api_->GetTextureHandle(layer.texture_id));
      if (handle) {
        const float width = std::min(240.0f, gui_->GetContentRegionAvail().x);
        if (width > 0.0f) RETURN_IF_ERROR(texture_preview_.Render(handle, width, 140.0f).status());
      }
    }
    if (gui_->Button("Delete Layer")) RETURN_IF_ERROR(model_.DeleteSelectedLayer());
  }

  gui_->Separator();
  if (gui_->Button("Save Theme")) {
    const absl::Status status = Save();
    if (!status.ok()) SetError(status);
  }
  gui_->SameLine();
  if (gui_->Button("Duplicate")) {
    const absl::Status status = Duplicate();
    if (!status.ok()) SetError(status);
  }
  draft = model_.draft();
  if (draft == nullptr) return absl::OkStatus();
  if (!draft->id.empty() &&
      delete_prompt_.Render(*gui_, "Delete Theme", draft->id,
                            absl::StrCat("Delete '", draft->name, "'?"), "ParallaxTheme")) {
    const absl::Status status = api_->DeleteParallaxTheme(draft->id);
    if (status.ok()) {
      model_.Close();
    } else {
      SetError(status);
    }
  }
  return absl::OkStatus();
}

}  // namespace zebes
