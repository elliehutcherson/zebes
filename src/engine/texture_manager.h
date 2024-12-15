#pragma once

#include <vector>

#include "SDL_render.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "engine/camera.h"
#include "engine/config.h"
#include "engine/vector.h"
#include "sqlite3.h"

namespace zebes {

struct Texture {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  // The offset from the left most point of the hitbox
  // at the scale of the source texture.
  int offset_x = 0;
  // The offset from the upper most point of the hitbox
  // at the scale of the source texture.
  int offset_y = 0;
  // The width that the texture will be rendered.
  int render_w = 0;
  // The height that the texture will be rendered.
  int render_h = 0;
  // The offset from the left most point of the hitbox
  // at the scale of the destination render.
  int render_offset_x = 0;
  // The offset from the upper most point of the hitbox
  // at the scale of the destination render.
  int render_offset_y = 0;
  // Number of frames to display this texture per cycle. If set to 0,
  // the texture will be displayed for the entire cycle.
  int frames = 0;
};

struct TexturePack {
  int type = 0;
  std::string name;
  std::string path;
  std::vector<Texture> textures;
  int ticks_per_sprite = 0;  // Deprecated
  int size() const { return textures.size(); }
};

class TextureManager {
 public:
  struct Options {
    const GameConfig *config;
    Camera *camera;
    SDL_Renderer *renderer;
  };

  static absl::StatusOr<std::unique_ptr<TextureManager>> Create(
      const Options &options);

  ~TextureManager() = default;

  absl::Status Render(uint16_t sprite_id, int index, Point position);

  SDL_Texture *Experiment();

 private:
  TextureManager(const Options &options);

  absl::Status Init();
  absl::Status LoadPack(int type);
  absl::Status LoadSdlTextures(TexturePack &pack);
  absl::Status LoadTextures(TexturePack &pack);

  const GameConfig *config_;
  SDL_Renderer *renderer_;
  Camera *camera_;
  absl::flat_hash_map<std::string, SDL_Texture *> path_to_texture_;
  absl::flat_hash_map<int, TexturePack> texture_packs_;
};

}  // namespace zebes
