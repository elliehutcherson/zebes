#pragma once

#include <string>

#include "SDL_rect.h"
#include "SDL_render.h"
#include "absl/status/status.h"
#include "common/common.h"

namespace zebes {

class CameraInterface {
 public:
  virtual ~CameraInterface() = default;
  // Update the position of the camera frame.
  virtual void Update(int center_x, int center_y) = 0;

  // Render textures in relation to the camera frame.
  virtual void Render(SDL_Texture *texture, const SDL_Rect *src_rect,
                      const SDL_Rect *dst_rect) const = 0;

  // Render textures statically, not within the frame of the camera.
  virtual void RenderStatic(SDL_Texture *texture, SDL_Rect *src_rect,
                            SDL_Rect *dst_rect) const = 0;

  // Render the outline of the player rectangle, no texture.
  virtual void RenderPlayerRectangle(SDL_Rect *dst_rect) = 0;

  // Render the outline of a tile rectangle, no texture.
  virtual void RenderTileRectangle(SDL_Rect *dst_rect) = 0;

  // Render a rectangle filled in with menu colors/opacity.
  virtual void RenderMenuRectangle(SDL_Rect *dst_rect) = 0;

  // Render text.
  virtual void RenderText(const std::string &message,
                          SDL_Rect *dst_rect) const = 0;

  // Render lines.
  virtual absl::Status RenderLines(RenderData render_data) = 0;

  virtual void RenderGrid() = 0;
};

}  // namespace zebes