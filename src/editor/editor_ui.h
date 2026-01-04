#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "api/api.h"
#include "common/sdl_wrapper.h"
#include "editor/animator.h"
#include "editor/blueprint/blueprint_editor.h"
#include "editor/config_editor.h"
#include "editor/level_editor/level_editor.h"
#include "editor/sprite_editor.h"
#include "editor/texture_editor.h"
#include "imgui.h"
#include "objects/sprite.h"
#include "objects/texture.h"

namespace zebes {

class EditorUi {
 public:
  static absl::StatusOr<std::unique_ptr<EditorUi>> Create(SdlWrapper* sdl, Api* api);
  ~EditorUi() = default;

  // Render all editor UI windows
  void Render();

 private:
  /**
   * @brief Helper to render a tab with consistent error handling.
   *
   * @param name The name of the tab.
   * @param render_fn The function to execute for rendering the tab content.
   *        Should return absl::Status.
   * @return true if the tab was rendered (BeginTabItem returned true).
   */
  bool RenderTab(const char* name, std::function<absl::Status()> render_fn);
  explicit EditorUi(SdlWrapper* sdl, Api* api);

  // Initialize owned objects.
  absl::Status Init();

  // Render sprite creation window
  void RenderSpriteCreation();

  /**
   * @brief Renders the list of existing sprites and handles selection.
   */
  void RenderSpriteList();

  /**
   * @brief Renders the inspector section for the selected sprite.
   */
  void RenderSpriteInspector();

  /**
   * @brief Renders the horizontal list of sprite frames for the selected sprite.
   */
  void RenderSpriteFrameList();

  /**
   * @brief Saves the changes made to the sprite frames.
   */
  void SaveSpriteFrames();

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
  void LoadSpriteTexture(const std::string& sprite_id);

  /**
   * @brief Refreshes the list of sprites from the API.
   */
  void RefreshSpriteList();

  /**
   * @brief Selects a sprite for editing.
   * @param sprite_id The ID of the sprite to select.
   */
  void SelectSprite(const std::string& sprite_id);

  void RefreshTextures();

  // Resources aquired at construction
  SdlWrapper* sdl_;
  Api* api_;
  std::unique_ptr<TextureEditor> texture_editor_;
  std::unique_ptr<ConfigEditor> config_editor_;
  std::unique_ptr<SpriteEditor> sprite_editor_;
  std::unique_ptr<BlueprintEditor> blueprint_editor_;
  std::unique_ptr<LevelEditor> level_editor_;

  // UI state buffers
  std::string sprite_path_buffer_;

  // Sprite config input buffers
  int sprite_x_input_;
  int sprite_y_input_;
  int sprite_w_input_;
  int sprite_h_input_;

  // Texture preview state
  std::vector<Texture> texture_list_;

  // Sprite list state
  std::vector<Sprite> sprite_list_;
  std::string selected_sprite_id_;
  bool is_creating_new_sprite_ = false;
  Sprite selected_sprite_;
  std::string edit_type_name_buffer_;
  std::vector<SpriteFrame> current_sprite_frames_;
  std::vector<SpriteFrame> original_sprite_frames_;
  SDL_Texture* sprite_texture_ = nullptr;

  // Interactive editing state
  int active_sprite_frame_index_ = -1;
  bool is_dragging_rect_ = false;
  ImVec2 drag_start_ = {0, 0};
  float full_texture_zoom_ = 1.0f;

  // Animation state
  std::unique_ptr<Animator> animator_;
  bool is_playing_animation_ = false;
  double animation_timer_ = 0.0;

  // Debug state
  bool show_debug_metrics_ = false;
};

}  // namespace zebes
