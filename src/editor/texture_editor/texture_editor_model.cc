#include "editor/texture_editor/texture_editor_model.h"

#include <algorithm>
#include <utility>

#include "absl/status/status.h"
#include "common/common.h"

namespace zebes {

void TextureEditorModel::SetTextures(std::vector<Texture> textures) {
  std::sort(textures.begin(), textures.end(),
            [](const Texture& a, const Texture& b) { return a.name < b.name; });
  textures_ = std::move(textures);
}

void TextureEditorModel::BeginNewTexture() {
  selected_texture_ = {};
  is_new_texture_ = true;
  SetEditName("");
}

void TextureEditorModel::SelectTexture(Texture texture) {
  selected_texture_ = std::move(texture);
  is_new_texture_ = false;
  SetEditName(selected_texture_.name);
}

bool TextureEditorModel::has_selection() const {
  return is_new_texture_ || !selected_texture_.id.empty();
}

Texture* TextureEditorModel::selected_texture() {
  return has_selection() ? &selected_texture_ : nullptr;
}

const Texture* TextureEditorModel::selected_texture() const {
  return has_selection() ? &selected_texture_ : nullptr;
}

void TextureEditorModel::SetSelectedPath(std::string path) {
  selected_texture_.path = std::move(path);
}

absl::StatusOr<Texture> TextureEditorModel::BuildTextureForCreate() const {
  if (!is_new_texture_) {
    return absl::FailedPreconditionError("Texture editor is not creating a texture");
  }
  if (selected_texture_.path.empty()) {
    return absl::InvalidArgumentError("Cannot create texture without a path");
  }

  Texture texture = selected_texture_;
  texture.name = edit_name_buffer_.c_str();
  return texture;
}

absl::StatusOr<Texture> TextureEditorModel::BuildTextureForUpdate() const {
  if (is_new_texture_ || selected_texture_.id.empty()) {
    return absl::FailedPreconditionError("No existing texture is selected");
  }

  Texture texture = selected_texture_;
  texture.name = edit_name_buffer_.c_str();
  return texture;
}

void TextureEditorModel::FinishCreate(Texture texture) { SelectTexture(std::move(texture)); }

void TextureEditorModel::FinishUpdate() {
  if (!is_new_texture_ && !selected_texture_.id.empty()) {
    selected_texture_.name = edit_name_buffer_.c_str();
  }
}

void TextureEditorModel::ZoomIn() { zoom_ = ClampZoom(zoom_ * 1.25f); }

void TextureEditorModel::ZoomOut() { zoom_ = ClampZoom(zoom_ * 0.8f); }

void TextureEditorModel::AdjustZoom(float factor) { zoom_ = ClampZoom(zoom_ * factor); }

TexturePreviewSize TextureEditorModel::CalculatePreviewSize(int texture_width,
                                                            int texture_height) const {
  constexpr float kBasePreviewWidth = 200.0f;
  float aspect = texture_width > 0 && texture_height > 0
                     ? static_cast<float>(texture_width) / static_cast<float>(texture_height)
                     : 1.0f;
  float width = kBasePreviewWidth * zoom_;
  return {.width = width, .height = width / aspect};
}

void TextureEditorModel::SetEditName(const std::string& name) {
  edit_name_buffer_ = name;
  edit_name_buffer_.resize(kMaxTextureNameLength + 1, '\0');
}

float TextureEditorModel::ClampZoom(float zoom) { return std::clamp(zoom, 0.1f, 10.0f); }

}  // namespace zebes
