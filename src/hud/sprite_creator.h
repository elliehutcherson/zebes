#pragma once

#include "engine/sprite_manager.h"
#include "hud/texture_creator.h"
#include "imgui.h"

namespace zebes {

class HudSpriteCreator {
 public:
  struct Options {
    SpriteManager *sprite_manager;
    HudTextureCreator *texture_creator;
  };

  static absl::StatusOr<HudSpriteCreator> Create(const Options &options);

  void RenderSpriteList();

  void RenderEditor();

  void Update();

 private:
  struct HudSprite {
    uint16_t sprite_id;
    std::unique_ptr<SpriteObject> sprite_object;
    SpriteConfig hud_config = *sprite_object->GetActiveSprite()->GetConfig();

    bool visible = false;
    bool paused = false;
    const std::string unique_name = absl::StrCat("##Sprite", sprite_id);
    const std::string label_main = absl::StrCat("Sprite ", unique_name);
    const std::string label_sprite_id = absl::StrCat("Sprite ID", unique_name);
    const std::string label_ticks_per_sprite =
        absl::StrCat("Ticks Per Sprite", unique_name);
    const std::string label_type_name = absl::StrCat("Type Name", unique_name);
  };

  struct EdittingSubSprite {
    bool active = false;
    bool show_hitbox = true;
    size_t index = 0;
    std::string unique_name = absl::StrCat("##SubSprite", index);

    SubSprite sub_sprite;
    std::vector<Point> hitbox;

    void set_index(size_t i) {
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

  HudSpriteCreator(const Options &options);

  absl::Status Init();

  absl::StatusOr<HudSprite *> FindSpriteById(uint16_t sprite_id);

  void RenderSpriteEditorSelection();

  void RenderSpriteCollapse(ImDrawList *draw_list, HudSprite &sprite);

  absl::Status RenderSubSpriteEditor(int index, bool &already_active);

  absl::Status RenderSubSpriteImage(const EdittingSubSprite& sub_sprite);

  SpriteManager *sprite_manager_;
  HudTextureCreator *texture_creator_;
  EdittingState editting_state_;
  // Sprites in asset window.
  absl::flat_hash_map<uint16_t, std::unique_ptr<HudSprite>> sprites_;
  std::optional<ImVec2> texture_offset_;
  std::optional<ImVec2> texture_size_;
};

}  // namespace zebes