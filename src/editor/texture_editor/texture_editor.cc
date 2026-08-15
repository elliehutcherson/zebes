#include "editor/texture_editor/texture_editor.h"

#include <optional>
#include <string>

#include "SDL_render.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/common.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "platform/sdl/sdl_texture_handle.h"

namespace zebes {
namespace {

// Names the texture browser so its result cannot be picked up by another
// panel's file dialog.
constexpr const char* kTextureDialogKey = "TextureOpenDlg_v2";

}  // namespace

absl::StatusOr<std::unique_ptr<TextureEditor>> TextureEditor::Create(Api* api, SdlWrapper* sdl,
                                                                     GuiInterface* gui) {
  if (api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null");
  }
  if (sdl == nullptr) {
    return absl::InvalidArgumentError("SdlWrapper must not be null");
  }
  if (gui == nullptr) {
    return absl::InvalidArgumentError("GUI must not be null");
  }
  return std::unique_ptr<TextureEditor>(new TextureEditor(api, sdl, gui));
}

TextureEditor::TextureEditor(Api* api, SdlWrapper* sdl, GuiInterface* gui)
    : api_(api), sdl_(sdl), gui_(gui) {
  RefreshTextures();
}

TextureEditor::~TextureEditor() { sdl_->DestroyTexture(preview_texture_); }

void TextureEditor::RefreshTextures() {
  absl::StatusOr<std::vector<Texture>> result = api_->GetAllTextures();
  if (!result.ok()) {
    LOG(ERROR) << "Failed to fetch textures for importer: " << result.status();
    model_.SetError(absl::StrCat("Could not list textures: ", result.status().message()));
    model_.SetTextures({});
    return;
  }
  model_.SetTextures(std::move(*result));
}

void TextureEditor::LoadPreview(const std::string& path) {
  const Texture* selected_texture = model_.selected_texture();
  if (selected_texture == nullptr || !selected_texture->id.empty()) return;

  sdl_->DestroyTexture(preview_texture_);
  preview_texture_ = nullptr;

  absl::StatusOr<SDL_Texture*> texture = sdl_->CreateTexture(path);
  if (!texture.ok()) {
    LOG(ERROR) << "Failed to load preview for importer: " << texture.status();
    model_.SetError(absl::StrCat("Could not open '", path, "': ", texture.status().message()));
    return;
  }

  preview_texture_ = *texture;
}

void TextureEditor::SelectTexture(const Texture& texture) {
  sdl_->DestroyTexture(preview_texture_);
  preview_texture_ = nullptr;
  model_.SelectTexture(texture);
}

void TextureEditor::DeleteSelectedTexture() {
  const Texture* selected_texture = model_.selected_texture();
  if (selected_texture == nullptr || selected_texture->id.empty()) return;

  const absl::Status status = api_->DeleteTexture(selected_texture->id);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to delete texture: " << status;
    // Shown as it arrives. A refusal already names the texture and lists every
    // referrer, so prefixing it with "Could not delete texture" would say the
    // subject twice.
    model_.SetError(status.message());
    return;
  }

  sdl_->DestroyTexture(preview_texture_);
  preview_texture_ = nullptr;
  model_.ClearSelection();
  RefreshTextures();
}

SDL_Texture* TextureEditor::PreviewTexture() const {
  if (preview_texture_ != nullptr) return preview_texture_;
  const Texture* selected_texture = model_.selected_texture();
  if (selected_texture == nullptr) return nullptr;

  // An unloaded texture yields an invalid handle, which resolves to null and
  // renders as "no preview" rather than failing.
  absl::StatusOr<TextureHandle> handle = api_->GetTextureHandle(selected_texture->id);
  if (!handle.ok()) return nullptr;
  return SdlTextureHandleAdapter::ToNative(*handle);
}

void TextureEditor::Render() {
  // Same shape as the tileset editor's banner, deliberately: an authoring tab
  // that reports failure differently from the tab beside it is one more thing
  // to learn.
  if (model_.error().has_value()) {
    gui_->TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Error: %s", model_.error()->c_str());
    gui_->SameLine();
    if (gui_->Button("Dismiss")) model_.ClearError();
  }

  // Use tables for list and inspector
  auto table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
  if (ScopedTable table = gui_->CreateScopedTable("TextureEditorTable", 2, table_flags); table) {
    gui_->TableSetupColumn("Texture List", ImGuiTableColumnFlags_WidthStretch);
    gui_->TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);

    gui_->TableNextRow();
    gui_->TableNextColumn();

    // Column 1: Texture List
    RenderTextureList();

    gui_->TableNextColumn();

    // Column 2: Details
    RenderTextureDetails();
  }

  // Handle File Dialog
  if (std::optional<std::string> chosen = gui_->DisplayFileDialog(kTextureDialogKey);
      chosen.has_value()) {
    model_.SetSelectedPath(*chosen);
    LoadPreview(*chosen);
  }

  gui_->Separator();

  // Preview below the table
  RenderPreview();
}

void TextureEditor::RenderTextureList() {
  gui_->Text("Imported Textures");

  // Refresh list if empty (or add a refresh button)
  if (gui_->Button("Refresh List")) {
    RefreshTextures();
  }

  gui_->SameLine();
  if (gui_->Button("New Texture")) {
    sdl_->DestroyTexture(preview_texture_);
    preview_texture_ = nullptr;
    model_.BeginNewTexture();
  }

  // List textures
  ScopedListBox list_box = gui_->CreateScopedListBox(
      "##Textures", ImVec2(-FLT_MIN, 10 * gui_->GetTextLineHeightWithSpacing()));
  if (list_box) {
    const Texture* selected_texture = model_.selected_texture();
    for (const Texture& texture : model_.textures()) {
      bool is_selected = (!model_.is_new_texture() && selected_texture != nullptr &&
                          selected_texture->id == texture.id);
      std::string label = texture.name_id();
      if (gui_->Selectable(label.c_str(), is_selected)) {
        SelectTexture(texture);
      }
      if (is_selected) {
        gui_->SetItemDefaultFocus();
      }
    }
  }
}

void TextureEditor::RenderTextureDetails() {
  Texture* selected_texture = model_.selected_texture();
  if (selected_texture == nullptr) {
    gui_->TextDisabled("Select a texture to edit.");
    return;
  }

  gui_->Text("Texture Details");
  gui_->Separator();

  // ID Field
  if (model_.is_new_texture()) {
    gui_->Text("ID: <Auto-Generated>");
  } else {
    // Read-only for existing
    gui_->LabelText("ID", "%s", selected_texture->id.c_str());
  }

  // Name Field
  gui_->InputText("Name", model_.edit_name_buffer().data(), model_.edit_name_buffer().size());

  // Read-only once imported: the path is what every tileset and sprite
  // resolves artwork through, so repointing it here would silently change
  // what they render. Import the new file as its own texture instead.
  gui_->LabelText("Path", "%s", selected_texture->path.c_str());

  if (model_.is_new_texture()) {
    gui_->SameLine();
    if (gui_->Button("Browse...")) {
      gui_->OpenFileDialog(kTextureDialogKey, "Choose File", ".png,.jpg,.jpeg,.bmp", ".");
    }
  }

  gui_->Spacing();

  if (model_.is_new_texture() && gui_->Button("Create")) {
    absl::StatusOr<Texture> texture = model_.BuildTextureForCreate();
    if (!texture.ok()) {
      LOG(ERROR) << texture.status();
      model_.SetError(texture.status().message());
      return;
    }

    absl::StatusOr<std::string> result = api_->CreateTexture(*texture);
    if (!result.ok()) {
      LOG(ERROR) << "Failed to create texture: " << result.status();
      model_.SetError(absl::StrCat("Could not create texture: ", result.status().message()));
      return;
    }

    texture->id = *result;

    // Reload from the manager to get canonical state (relative path, etc.).
    absl::StatusOr<Texture*> loaded = api_->GetTexture(*result);
    if (!loaded.ok()) {
      LOG(ERROR) << "Created texture but failed to reload: " << loaded.status();
      // The texture exists; only the canonical reload failed. Say so rather
      // than reporting nothing, because the two states differ on disk.
      model_.FinishCreate(*texture);
      model_.SetError(absl::StrCat("Created the texture but could not reload it: ",
                                   loaded.status().message()));
    } else {
      model_.FinishCreate(**loaded);
    }

    sdl_->DestroyTexture(preview_texture_);
    preview_texture_ = nullptr;
    RefreshTextures();
    return;
  }

  if (gui_->Button("Save")) {
    absl::StatusOr<Texture> texture = model_.BuildTextureForUpdate();
    if (!texture.ok()) {
      LOG(ERROR) << texture.status();
      model_.SetError(texture.status().message());
      return;
    }
    absl::Status status = api_->UpdateTexture(*texture);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to update texture: " << status;
      model_.SetError(absl::StrCat("Could not save texture: ", status.message()));
      return;
    }

    model_.FinishUpdate();
    RefreshTextures();
  }

  // Nothing is on disk yet for a texture being created, so there is nothing to
  // delete -- abandoning it is what New Texture and picking a row already do.
  if (model_.is_new_texture()) return;

  const std::string question = absl::StrCat("Delete '", selected_texture->name,
                                            "'? Its definition and its image file both go.");
  if (delete_texture_prompt_.Render(*gui_, "Delete", selected_texture->id, question, "Texture")) {
    DeleteSelectedTexture();
  }
}

void TextureEditor::RenderZoom() {
  // Zoom controls
  if (gui_->Button("-")) {
    model_.ZoomOut();
  }
  gui_->SameLine();
  if (gui_->Button("+")) {
    model_.ZoomIn();
  }
  gui_->SameLine();
  if (gui_->Button("Reset Zoom")) {
    model_.ResetZoom();
  }
  gui_->SameLine();
  gui_->Text("Zoom: %.1fx", model_.zoom());

  int w = 0, h = 0;
  if (SDL_Texture* texture = PreviewTexture(); texture != nullptr) {
    SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
  }
  TexturePreviewSize preview_size = model_.CalculatePreviewSize(w, h);
  preview_w_ = preview_size.width;
  preview_h_ = preview_size.height;

  gui_->Text("Size: %dx%d", w, h);
}

void TextureEditor::RenderPreview() {
  RenderZoom();

  gui_->Text("Texture Preview");

  // Display the image with zoom applied
  ScopedChild child = gui_->CreateScopedChild("PreviewRegion", ImVec2(0, 400), true,
                                              ImGuiWindowFlags_HorizontalScrollbar);

  SDL_Texture* texture = PreviewTexture();
  if (texture == nullptr) {
    gui_->TextDisabled("No texture loaded.");
    return;
  }

  // Mouse wheel zoom when hovering over preview
  if (gui_->IsWindowHovered()) {
    float wheel = gui_->GetIO().MouseWheel;
    if (wheel != 0.0f) {
      model_.AdjustZoom(1.0f + wheel * 0.1f);
    }
  }

  gui_->Image(reinterpret_cast<ImTextureID>(texture), ImVec2(preview_w_, preview_h_));
}

}  // namespace zebes
