#include "map.h"

#include <string>

#include "SDL_image.h"
#include "SDL_render.h"

#include "absl/status/status.h"

#include "camera.h"
#include "config.h"

namespace zebes {

Map::Map(const GameConfig *config, const Camera *camera)
    : config_(config), camera_(camera) {
  dst_rect_.h = 0;
  dst_rect_.w = 0;
  dst_rect_.x = 0;
  dst_rect_.y = 0;
};

absl::Status Map::Init(SDL_Renderer *renderer) {
  SDL_Surface *tmp_surface = IMG_Load(config_->paths.background().c_str());
  if (tmp_surface == nullptr) {
    return absl::AbortedError("Failed to create map tmp_surface.");
  }
  texture_ = SDL_CreateTextureFromSurface(renderer, tmp_surface);
  if (texture_ == nullptr) {
    return absl::AbortedError("Failed to create map.");
  }
  SDL_FreeSurface(tmp_surface);
  return absl::OkStatus();
}

void Map::Render() { camera_->RenderStatic(texture_, nullptr, nullptr); }

} // namespace zebes