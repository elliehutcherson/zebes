#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ImGuiFileDialog.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "common/sdl_wrapper.h"
#include "objects/texture.h"

namespace zebes {

class TextureEditor {
 public:
  static absl::StatusOr<std::unique_ptr<TextureEditor>> Create(Api* api, SdlWrapper* sdl);

  ~TextureEditor();

  void Render();

  const std::vector<Texture>& GetTextureList() const { return texture_list_; }

 private:
  TextureEditor(Api* api, SdlWrapper* sdl);

  void RenderImport();
  void RenderTextureList();
  void RenderTextureDetails();
  void RenderZoom();
  void RenderPreview();

  // Refreshes the internal texture list from the API
  void RefreshTextures();
  void LoadPreview(const std::string& path);
  // Selects a texture and sets up the editing buffer
  void SelectTexture(const Texture& texture);

  Api* api_;
  SdlWrapper* sdl_;

  // UI state buffers
  Texture selected_texture_;
  bool new_texture_ = false;
  std::string edit_name_buffer_;

  // Texture preview state
  std::vector<Texture> texture_list_;

  // Zoom control set on every frame
  float zoom_ = 1.0f;
  float preview_w_ = 0;
  float preview_h_ = 0;

  // Import state
  ImGuiFileDialog file_dialog_;

  // Deletion state
  std::string texture_to_delete_;
  bool show_delete_popup_ = false;
};

}  // namespace zebes
