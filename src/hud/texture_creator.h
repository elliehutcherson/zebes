#pragma once

#include "absl/status/statusor.h"
#include "api/api.h"
#include "engine/sprite_manager.h"
#include "imgui.h"

namespace zebes {

struct HudTexture {
  SDL_Texture *texture;
  std::string name;
  std::string path;
  int width = 0;
  int height = 0;
};

class HudTextureCreator {
 public:
  struct Options {
    SpriteManager *sprite_manager;
    Api *api;
  };

  static absl::StatusOr<HudTextureCreator> Create(const Options &options);

  absl::StatusOr<HudTexture *> FindTextureByIndex(int index);

  const char **GetTextureNames();

  void Render();

  void Update();

 private:
  static constexpr char kTextureNone[] = "None";

  HudTextureCreator(const Options &options);

  void RenderTexture(ImDrawList *draw_list, HudTexture &hud_texture,
                     int texture_index, bool render_collpase = true);

  absl::Status ImportTexture();

  SpriteManager *sprite_manager_;
  Api *api_;
  std::map<std::string, HudTexture> name_to_textures_;
  std::vector<const char *> texture_names_;
  char texture_import_path_[4096];
  bool show_file_dialog_ = false;
  std::optional<std::string> temp_texture_path_;
  std::optional<HudTexture> temp_texture_;
};

}  // namespace zebes