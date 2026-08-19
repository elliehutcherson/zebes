
#pragma once

#include <memory>
#include <string>

#include "SDL_render.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/animator.h"
#include "editor/confirm_prompt.h"
#include "editor/gui_interface.h"
#include "editor/sprite_editor/sprite_editor_model.h"
#include "imgui.h"
#include "objects/sprite.h"
#include "platform/sdl/sdl_texture_handle.h"

namespace zebes {

// The Sprite tab: pick a sprite, cut its frames out of a texture atlas, and
// watch the result animate.
//
// Editing state lives in SpriteEditorModel, which holds edits until an explicit
// save; this class draws that state and forwards commands to the Api. The
// members below are the exception, being playback and drag state that no other
// view needs.
//
// Frames are edited two ways that write the same fields -- the numeric inputs
// under each frame, and dragging a rectangle over the full texture view -- and
// both clamp against the texture's real dimensions every frame, so replacing a
// texture with a smaller one pulls existing frames back inside it.
class SpriteEditor {
 public:
  static absl::StatusOr<std::unique_ptr<SpriteEditor>> Create(Api* api, SdlWrapper* sdl,
                                                              GuiInterface* gui);

  ~SpriteEditor() = default;

  void Render();

 private:
  SpriteEditor(Api* api, SdlWrapper* sdl, GuiInterface* gui);

  // Commands. These mutate the model or call the Api and draw nothing.
  void RefreshSpriteList();
  void LoadSpriteTexture(const std::string& sprite_id);
  void SelectSprite(const std::string& sprite_id);
  void CreateSprite();
  void UpdateSprite();
  void DeleteSprite(const std::string& sprite_id);
  void SaveSpriteFrames();

  // Top row: the sprite list and its inspector.
  void RenderSpriteSelection();
  void RenderSpriteList();
  void RenderSpriteMeta();
  void RenderSpriteAnimation();

  // Bottom row: the full texture and the frames cut from it.
  void RenderFullTextureView();
  void RenderSpriteFrameList();
  void RenderSpriteFrameItem(int index, SpriteFrame& frame);

  SDL_Texture* SdlTexture() { return SdlTextureHandleAdapter::ToNative(model_.texture()); }
  ImTextureID ImTextureId() { return reinterpret_cast<ImTextureID>(SdlTexture()); }

  Api* api_;
  SdlWrapper* sdl_;
  GuiInterface* gui_;

  SpriteEditorModel model_;

  // Deleting a sprite destroys every frame authored on it, so it asks first.
  ConfirmPrompt delete_sprite_prompt_;

  // Preview playback. The timer accumulates real time so the preview runs at
  // the animation's authored tick rate rather than the editor's frame rate.
  std::unique_ptr<Animator> animator_ = std::make_unique<Animator>();
  bool is_playing_animation_ = false;
  double animation_timer_ = 0.0;

  // Anchor of an in-progress rectangle drag over the full texture view, in
  // texture pixels. Only meaningful while is_dragging_rect_ is set.
  bool is_dragging_rect_ = false;
  ImVec2 drag_start_ = {0, 0};
};

}  // namespace zebes
