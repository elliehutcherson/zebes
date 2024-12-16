#pragma once

#include <string>

#include "SDL_pixels.h"
#include "SDL_rect.h"
#include "SDL_render.h"
#include "SDL_ttf.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "camera_interface.h"
#include "config.h"
#include "vector.h"

namespace zebes {

class Camera : public CameraInterface {
 public:
  struct Options {
    const GameConfig *config;
    SDL_Renderer *renderer;
  };

  static absl::StatusOr<std::unique_ptr<Camera>> Create(const Options &options);

  Camera(const GameConfig *config, SDL_Renderer *renderer);

  // Update the position of the camera frame.
  void Update(int center_x, int center_y) override;

  // Render textures in relation to the camera frame.
  void Render(SDL_Texture *texture, const SDL_Rect *src_rect,
              const SDL_Rect *dst_rect) const override;

  // Render textures statically, not within the frame of the camera.
  void RenderStatic(SDL_Texture *texture, SDL_Rect *src_rect,
                    SDL_Rect *dst_rect) const override;

  // Render the outline of the player rectangle, no texture.
  void RenderPlayerRectangle(SDL_Rect *dst_rect) override;

  // Render the outline of a tile rectangle, no texture.
  void RenderTileRectangle(SDL_Rect *dst_rect) override;

  // Render a rectangle filled in with menu colors/opacity.
  void RenderMenuRectangle(SDL_Rect *dst_rect) override;

  // Render text.
  void RenderText(const std::string &message,
                  SDL_Rect *dst_rect) const override;

  // Render lines.
  absl::Status RenderLines(const std::vector<Point> &vertices, DrawColor color,
                           bool static_position = true) override;

  void RenderGrid() override;

 private:
  // Initialize the camera. Could fail when creating font.
  absl::Status Init();

  // Update the current color.
  void UpdateColor(DrawColor color);

  // Adjust the destination rectangle to match camera position.
  SDL_Rect Adjust(const SDL_Rect *dst_rect) const;

  // Adjust the destination point to match camera position.
  SDL_Point Adjust(const SDL_Point *point) const;

  const GameConfig *config_;
  SDL_Renderer *renderer_;
  SDL_Rect rect_;
  DrawColor current_color_ = kColorPlayer;
  TTF_Font *font_;
  SDL_Color font_color_ = {255, 255, 255};
  std::vector<std::vector<SDL_Point>> grid_x_;
  std::vector<std::vector<SDL_Point>> grid_y_;
};

}  // namespace zebes