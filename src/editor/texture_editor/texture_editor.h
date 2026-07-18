#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "common/sdl_wrapper.h"
#include "editor/gui_interface.h"
#include "editor/texture_editor/texture_editor_model.h"

namespace zebes {

class TextureEditor {
 public:
  static absl::StatusOr<std::unique_ptr<TextureEditor>> Create(Api* api, SdlWrapper* sdl,
                                                               GuiInterface* gui);

  ~TextureEditor();

  void Render();

  const std::vector<Texture>& GetTextureList() const { return model_.textures(); }

 private:
  TextureEditor(Api* api, SdlWrapper* sdl, GuiInterface* gui);

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
  SDL_Texture* PreviewTexture() const;

  Api* api_;
  SdlWrapper* sdl_;
  GuiInterface* gui_;

  TextureEditorModel model_;
  SDL_Texture* preview_texture_ = nullptr;

  // Preview dimensions calculated for the current frame.
  float preview_w_ = 0;
  float preview_h_ = 0;
};

}  // namespace zebes
