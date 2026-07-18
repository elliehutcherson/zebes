
#pragma once

#include <memory>
#include <string>

#include "SDL_render.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/animator.h"
#include "editor/gui_interface.h"
#include "editor/sprite_editor/sprite_editor_model.h"
#include "imgui.h"
#include "objects/sprite.h"
#include "platform/sdl/sdl_texture_handle.h"

namespace zebes {

class SpriteEditor {
 public:
  static absl::StatusOr<std::unique_ptr<SpriteEditor>> Create(Api* api, SdlWrapper* sdl,
                                                              GuiInterface* gui);

  ~SpriteEditor() = default;

  void Render();

 private:
  SpriteEditor(Api* api, SdlWrapper* sdl, GuiInterface* gui);

  // State manager methods that do no rendering
  void RefreshSpriteList();
  void LoadSpriteTexture(const std::string& sprite_id);
  void SelectSprite(const std::string& sprite_id);
  void CreateSprite();
  void UpdateSprite();
  void DeleteSprite(const std::string& sprite_id);
  void SaveSpriteFrames();

  // Render methods
  void RenderSpriteSelection();
  void RenderSpriteList();
  void RenderSpriteMeta();
  void RenderSpriteAnimation();

  // Second row
  void RenderFullTextureView();
  void RenderSpriteFrameList();
  void RenderSpriteFrameItem(int index, SpriteFrame& frame);

  SDL_Texture* SdlTexture() {
    return SdlTextureHandleAdapter::ToNative(model_.sprite().texture_handle);
  }
  ImTextureID ImTextureId() { return (ImTextureID)(SdlTexture()); }

  Api* api_;
  SdlWrapper* sdl_;
  GuiInterface* gui_;

  SpriteEditorModel model_;

  // Animation state
  std::unique_ptr<Animator> animator_ = std::make_unique<Animator>();
  bool is_playing_animation_ = false;
  double animation_timer_ = 0.0;

  // Sprite Frame editing state
  bool is_dragging_rect_ = false;
  ImVec2 drag_start_ = {0, 0};
};

}  // namespace zebes
