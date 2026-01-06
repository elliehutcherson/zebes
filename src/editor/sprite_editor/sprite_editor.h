
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "SDL_render.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "common/sdl_wrapper.h"
#include "editor/animator.h"
#include "imgui.h"
#include "objects/sprite.h"
#include "objects/texture.h"

namespace zebes {

class SpriteEditor {
 public:
  static absl::StatusOr<std::unique_ptr<SpriteEditor>> Create(Api* api, SdlWrapper* sdl);

  ~SpriteEditor() = default;

  void Render();

 private:
  SpriteEditor(Api* api, SdlWrapper* sdl);

  // State manager methods that do no rendering
  void RefreshSpriteList();
  void LoadSpriteTexture(const std::string& sprite_id);
  void SelectSprite(const std::string& sprite_id);
  void UpsertSprite(const std::string& sprite_id);
  void UpdateSprite(const std::string& sprite_id);
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

  SDL_Texture* SdlTexture() { return reinterpret_cast<SDL_Texture*>(sprite_.sdl_texture); }
  ImTextureID ImTextureId() { return (ImTextureID)(SdlTexture()); }

  Api* api_;
  SdlWrapper* sdl_;

  // UI state buffers
  std::string path_buffer_;

  // Sprite config input buffers
  std::string id_input_;
  std::string name_input_;
  int x_input_;
  int y_input_;
  int w_input_;
  int h_input_;

  // Texture preview state
  SdlWrapper* sdl_wrapper_ = nullptr;
  std::vector<Texture> texture_list_;

  // Sprite list state
  std::vector<Sprite> sprite_list_;
  bool new_sprite_ = false;
  Sprite sprite_;
  std::string edit_name_buffer_;

  // Animation state
  std::unique_ptr<Animator> animator_ = std::make_unique<Animator>();
  bool is_playing_animation_ = false;
  double animation_timer_ = 0.0;

  // Sprite Frame editing state
  bool is_dragging_rect_ = false;
  int active_frame_index_ = -1;
  float full_texture_zoom_ = 1.0f;
  ImVec2 drag_start_ = {0, 0};
  std::vector<SpriteFrame> original_frames_;
};

}  // namespace zebes