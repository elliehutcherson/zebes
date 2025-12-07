#pragma once

#include <string>
#include <vector>

#include "SDL.h"
#include "api/api.h"
#include "editor/animator.h"
#include "imgui.h"
#include "objects/texture.h"

namespace zebes {

class EditorUi {
 public:
  EditorUi();
  ~EditorUi() = default;

  // Render all editor UI windows
  void Render(Api* api);

  // Set renderer for creating textures
  void SetRenderer(SDL_Renderer* renderer) { renderer_ = renderer; }

 private:
  // Render texture import window
  void RenderTextureImport(Api* api);

  // Render sprite creation window
  void RenderSpriteCreation(Api* api);

  // Render game config editor
  void RenderConfigEditor(Api* api);

  /**
   * @brief Renders the list of existing sprites and handles selection.
   * @param api Pointer to the API for data fetching.
   */
  void RenderSpriteList(Api* api);

  /**
   * @brief Renders the horizontal list of sprite frames for the selected sprite.
   * @param api Pointer to the API for saving changes.
   */
  void RenderSpriteFrameList(Api* api);

  /**
   * @brief Renders a single sprite frame item in the horizontal list.
   * @param index The index of the sprite frame.
   * @param frame The sprite frame data reference.
   */
  void RenderSpriteFrameItem(size_t index, SpriteFrame& frame);

  /**
   * @brief Renders the full texture view with interactive editing capabilities.
   */
  void RenderFullTextureView();

  /**
   * @brief Helper to load a texture for given sprite ID.
   * @param sprite_id The ID of the sprite to load texture for.
   */
  void LoadSpriteTexture(int sprite_id);

  // Load texture for preview
  // Load texture for preview
  void LoadTexturePreview(const std::string& path);

  void RefreshTextures(Api* api);

  // UI state buffers
  std::string texture_path_buffer_;
  std::string sprite_path_buffer_;
  int selected_texture_id_;

  // Sprite config input buffers
  int sprite_id_input_;
  int sprite_type_input_;
  int sprite_x_input_;
  int sprite_y_input_;
  int sprite_w_input_;
  int sprite_h_input_;

  // Texture preview state
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* preview_texture_ = nullptr;
  std::vector<Texture> texture_list_;
  float texture_preview_zoom_ = 1.0f;

  // Sprite list state
  std::vector<SpriteConfig> sprite_list_;
  int selected_sprite_id_ = 0;
  bool is_creating_new_sprite_ = false;
  SpriteConfig selected_sprite_config_;
  std::string edit_type_name_buffer_;
  std::vector<SpriteFrame> current_sprite_frames_;
  std::vector<SpriteFrame> original_sprite_frames_;
  SDL_Texture* sprite_texture_ = nullptr;

  // Interactive editing state
  int active_sprite_frame_index_ = -1;
  bool is_dragging_rect_ = false;
  ImVec2 drag_start_ = {0, 0};
  float full_texture_zoom_ = 1.0f;

  // Config editor state
  GameConfig local_config_;
  bool config_loaded_ = false;

  // Animation state
  std::unique_ptr<Animator> animator_;
  bool is_playing_animation_ = false;
  double animation_timer_ = 0.0;
};

}  // namespace zebes
