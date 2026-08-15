#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "common/sdl_wrapper.h"
#include "editor/confirm_prompt.h"
#include "editor/gui_interface.h"
#include "editor/texture_editor/texture_editor_model.h"

namespace zebes {

class TextureEditor {
 public:
  // Reaches the details column on its own, the way TilesetEditorTestPeer
  // reaches the atlas gestures. Driving deletion through the whole Render would
  // mean standing up the table, the list box and the preview to exercise one
  // button.
  friend class TextureEditorTestPeer;

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
  // Removes the selected texture's definition and its image file. Refusals
  // land in the model's error banner, which names what still references it.
  void DeleteSelectedTexture();
  SDL_Texture* PreviewTexture() const;

  Api* api_;
  SdlWrapper* sdl_;
  GuiInterface* gui_;

  TextureEditorModel model_;
  SDL_Texture* preview_texture_ = nullptr;

  // Armed against the texture's ID, so selecting a different one while the
  // question is up disarms it instead of repointing it.
  ConfirmPrompt delete_texture_prompt_;

  // Preview dimensions calculated for the current frame.
  float preview_w_ = 0;
  float preview_h_ = 0;
};

}  // namespace zebes
