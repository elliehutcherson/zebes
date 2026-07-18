#pragma once

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/texture.h"

namespace zebes {

struct TexturePreviewSize {
  float width = 0.0f;
  float height = 0.0f;
};

// Owns TextureEditor's authoring state and preview calculations without
// depending on ImGui, SDL, or the application API.
class TextureEditorModel {
 public:
  void SetTextures(std::vector<Texture> textures);
  const std::vector<Texture>& textures() const { return textures_; }

  void BeginNewTexture();
  void SelectTexture(Texture texture);
  bool is_new_texture() const { return is_new_texture_; }
  bool has_selection() const;

  Texture* selected_texture();
  const Texture* selected_texture() const;
  void SetSelectedPath(std::string path);

  std::string& edit_name_buffer() { return edit_name_buffer_; }
  const std::string& edit_name_buffer() const { return edit_name_buffer_; }

  absl::StatusOr<Texture> BuildTextureForCreate() const;
  absl::StatusOr<Texture> BuildTextureForUpdate() const;
  void FinishCreate(Texture texture);
  void FinishUpdate();

  float zoom() const { return zoom_; }
  void ZoomIn();
  void ZoomOut();
  void AdjustZoom(float factor);
  void ResetZoom() { zoom_ = 1.0f; }

  TexturePreviewSize CalculatePreviewSize(int texture_width, int texture_height) const;

 private:
  void SetEditName(const std::string& name);
  static float ClampZoom(float zoom);

  std::vector<Texture> textures_;
  Texture selected_texture_;
  bool is_new_texture_ = false;
  std::string edit_name_buffer_;
  float zoom_ = 1.0f;
};

}  // namespace zebes
