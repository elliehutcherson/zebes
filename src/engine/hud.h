#pragma once

#include <cstdint>
#include <vector>

#include "SDL_render.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "engine/config.h"
#include "engine/controller.h"
#include "engine/focus.h"
#include "engine/sprite_interface.h"
#include "engine/sprite_manager.h"
#include "engine/sprite_object.h"
#include "imgui.h"

namespace zebes {

class Hud {
 public:
  struct Options {
    const GameConfig *config;
    const Focus *focus;
    Controller *controller;
    SpriteManager *sprite_manager;

    SDL_Window *window;
    SDL_Renderer *renderer;
  };

  static absl::StatusOr<std::unique_ptr<Hud>> Create(Options options);

  ~Hud() = default;

  // Inject events to the controller, like if ImGui is capturing the mouse.
  void InjectEvents();

  void Update();

  void Render();

 private:
  struct Scene {
    std::string name;
    int index = 0;
    char save_path[4096] = "";
    char import_path[4096] = "";
  };

  struct HudTexture {
    SDL_Texture *texture;
    std::string name;
    std::string path;
    int width = 0;
    int height = 0;
  };

  struct HudSprite {
    uint16_t sprite_id;
    std::unique_ptr<SpriteObject> sprite_object;
    SpriteConfig hud_config = *sprite_object->GetActiveSprite()->GetConfig();

    bool visible = false;
    bool paused = false;
    std::string unique_name = absl::StrCat("##Sprite", sprite_id);
    std::string label_main = absl::StrCat("Sprite ", unique_name);
    std::string label_sprite_id = absl::StrCat("Sprite ID", unique_name);
    std::string label_ticks_per_sprite =
        absl::StrCat("Ticks Per Sprite", unique_name);
    std::string label_type_name = absl::StrCat("Type Name", unique_name);
  };

  struct EdittingSubSprite {
    bool active = false;
    bool show_hitbox = true;
    size_t index = 0;
    std::string unique_name = absl::StrCat("##SubSprite", index);
 
    SubSprite sub_sprite;
    std::vector<Point> hitbox;
    
    void set_index (size_t i) {
      index = i;
      unique_name = absl::StrCat("##SubSprite", index);
    }
  };

  struct EdittingState {
    uint16_t sprite_id = 0;
    uint16_t ticks_per_sprite = 0;
    uint16_t type = 0;
    std::string type_name;
    bool interactable = false;

    int zoom = 1;
    int texture_index = 0;
    std::optional<ImVec2> selection_start;
    std::optional<ImVec2> selection_end;
    std::vector<EdittingSubSprite> sub_sprites;

    int GetActiveIndex() {
      for (size_t i = 0; i < sub_sprites.size(); i++) {
        if (sub_sprites[i].active) return i;
      }
      return -1;
    }
  };


  static constexpr char kTextureNone[] = "None";

  static ImVec2 GetSourceCoordinates(const HudTexture& hud_texture, Point coordinates);
   
  Hud(Options options);

  absl::Status Init(SDL_Window *window, SDL_Renderer *renderer);

  void RenderDemoMode();
  void RenderCreatorMode();
  void RenderPlayerMode();
  void RenderWindowConfig();
  void RenderBoundaryConfig();
  void RenderTileConfig();
  void RenderCollisionConfig();
  void RenderSceneWindowPrimary();
  void RenderSceneWindow(int index);

  void RenderTexturesWindow();
  void RenderTextureCollapse(ImDrawList *draw_list, HudTexture& hud_texture, int texture_index);

  void RenderSpritesWindow();
  void RenderSpriteCollapse(ImDrawList *draw_list, HudSprite &sprite);
  void RenderSpriteEditor();
  absl::Status RenderSubSpriteEditor(int index, bool& already_active);

  void RenderTerminalWindow();

  absl::StatusOr<HudTexture*> FindTextureByIndex(int index) {
    if (index < 0 || index >= texture_names_.size()) {
      return absl::InvalidArgumentError("Texture index out of bounds.");
    }
    auto it = name_to_textures_.find(texture_names_[index]);
    if (it == name_to_textures_.end()) {
      return absl::InvalidArgumentError("Texture not found.");
    }
    return &it->second;
  }

  absl::StatusOr<HudSprite*> FindSpriteById(uint16_t sprite_id) {
    auto it = sprites_.find(sprite_id);
    if (it == sprites_.end()) {
      return absl::InvalidArgumentError("Sprite not found.");
    }
    return it->second.get();
  }


  // Actions to take from the hud interface.
  void SaveConfig();

  const GameConfig *config_;
  const Focus *focus_;
  Controller *controller_;
  SpriteManager *sprite_manager_;

  SDL_Renderer *renderer_;
  ImGuiIO *imgui_io_;
  ImGuiContext *imgui_context_;
  ImVec4 clear_color_;

  // Copy of the config to modify and save.
  GameConfig hud_config_ = *config_;

  // If the log size hasn't changed, we don't need to update the log.
  int log_size_ = 0;

  bool rebuild_docks_ = false;

  // If creating scenes, store them here.
  int active_scene_ = -1;
  std::vector<Scene> scenes_;
  absl::flat_hash_set<int> removed_scenes_;

  // Sprites in asset window.
  absl::flat_hash_map<uint16_t, std::unique_ptr<HudSprite>> sprites_;

  // Textures in asset window.
  std::map<std::string, HudTexture> name_to_textures_;
  std::vector<const char *> texture_names_;

  // Editting state for the sprite editor.
  EdittingState editting_state_;

  char texture_import_path_[4096];
};

}  // namespace zebes