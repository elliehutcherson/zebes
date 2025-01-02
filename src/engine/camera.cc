#include "camera.h"

#include <memory>
#include <string>

#include "SDL_rect.h"
#include "SDL_ttf.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "config.h"
#include "vector.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Camera>> Camera::Create(const Options &options) {
  if (options.config == nullptr) {
    return absl::InvalidArgumentError("Config must not be null.");
  }
  if (options.renderer == nullptr) {
    return absl::InvalidArgumentError("Renderer must not be null.");
  }

  std::unique_ptr<Camera> camera(new Camera(options.config, options.renderer));
  absl::Status result = camera->Init();
  if (!result.ok()) return result;

  return camera;
}

Camera::Camera(const GameConfig *config, SDL_Renderer *renderer)
    : config_(config), renderer_(renderer) {}

absl::Status Camera::Init() {
  rect_.h = config_->window.height;
  rect_.w = config_->window.width;
  rect_.x = config_->boundaries.x_min;
  rect_.y = config_->boundaries.y_min;

  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
  if (SDL_SetRenderDrawColor(renderer_, 0, 255, 0, 255) != 0) {
    return absl::InternalError("Camera: Unable to set draw color.");
  }

  font_ = TTF_OpenFont(config_->paths.hud_font().c_str(), 12);
  if (font_ == nullptr) {
    std::string error =
        absl::StrCat("Camera: Unable to create font. ", TTF_GetError());
    return absl::InternalError(error);
  }

  int max_num_tiles_x = config_->window.width / config_->tiles.render_width();
  for (int i = 0; i < max_num_tiles_x; i++) {
    std::vector<SDL_Point> column;
    SDL_Point p1 = {.x = i * config_->tiles.render_width(), .y = 0};
    SDL_Point p2 = {.x = i * config_->tiles.render_width(),
                    .y = config_->window.height};
    grid_x_.push_back({p1, p2});
  }

  int max_num_tiles_y = config_->window.height / config_->tiles.render_height();
  for (int i = 0; i < max_num_tiles_y; i++) {
    std::vector<SDL_Point> row;
    SDL_Point p1 = {.x = 0, .y = i * config_->tiles.render_height()};
    SDL_Point p2 = {.x = config_->window.width,
                    .y = i * config_->tiles.render_height()};
    grid_y_.push_back({p1, p2});
  }

  return absl::OkStatus();
}

void Camera::Update(int center_x, int center_y) {
  // We want to get the middle point of the player rectangle.
  int updated_x = center_x - (rect_.w / 2);
  updated_x = std::max(config_->boundaries.x_min, updated_x);
  updated_x = std::min(config_->boundaries.x_max, updated_x);

  int updated_y = center_y - (rect_.h / 2);
  updated_y = std::max(config_->boundaries.y_min, updated_y);
  updated_y = std::min(config_->boundaries.y_max, updated_y);

  rect_.x = updated_x;
  rect_.y = updated_y;
  return;
}

void Camera::UpdateColor(DrawColor color) {
  if (current_color_ == color) return;
  current_color_ = color;
  switch (color) {
    case kColorPlayer:
      SDL_SetRenderDrawColor(renderer_, 0, 0, 255, 255);
      break;
    case kColorTile:
      SDL_SetRenderDrawColor(renderer_, 0, 255, 0, 255);
      break;
    case kColorMenu:
      SDL_SetRenderDrawColor(renderer_, 100, 100, 100, 100);
      break;
    case kColorCollide:
      SDL_SetRenderDrawColor(renderer_, 255, 0, 0, 255);
      break;
    case kColorGrid:
      SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
      break;
    default:
      break;
  }
}

SDL_Rect Camera::Adjust(const SDL_Rect *dst_rect) const {
  return {
      .x = dst_rect->x - rect_.x,
      .y = dst_rect->y - rect_.y,
      .w = dst_rect->w,
      .h = dst_rect->h,
  };
}

SDL_Point Camera::Adjust(const SDL_Point *point) const {
  return {
      .x = point->x - rect_.x,
      .y = point->y - rect_.y,
  };
}

void Camera::Render(SDL_Texture *texture, const SDL_Rect *src_rect,
                    const SDL_Rect *dst_rect) const {
  SDL_Rect adjusted = Adjust(dst_rect);
  SDL_RenderCopy(renderer_, texture, src_rect, &adjusted);
}

void Camera::RenderStatic(SDL_Texture *texture, SDL_Rect *src_rect,
                          SDL_Rect *dst_rect) const {
  SDL_RenderCopy(renderer_, texture, src_rect, dst_rect);
}

void Camera::RenderPlayerRectangle(SDL_Rect *dst_rect) {
  UpdateColor(DrawColor::kColorPlayer);
  SDL_Rect adjusted = Adjust(dst_rect);
  SDL_RenderDrawRect(renderer_, &adjusted);
}

void Camera::RenderTileRectangle(SDL_Rect *dst_rect) {
  UpdateColor(DrawColor::kColorTile);
  SDL_Rect adjusted = Adjust(dst_rect);
  SDL_RenderDrawRect(renderer_, &adjusted);
}

void Camera::RenderMenuRectangle(SDL_Rect *dst_rect) {
  UpdateColor(DrawColor::kColorMenu);
  SDL_RenderFillRect(renderer_, dst_rect);
}

void Camera::RenderText(const std::string &message, SDL_Rect *dst_rect) const {
  SDL_Surface *surface =
      TTF_RenderText_Blended_Wrapped(font_, message.c_str(), font_color_, 170);
  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer_, surface);
  SDL_RenderCopy(renderer_, texture, nullptr, dst_rect);
  SDL_FreeSurface(surface);
  SDL_DestroyTexture(texture);
}

absl::Status Camera::RenderLines(const std::vector<Point> &vertices,
                                 DrawColor color, bool static_position) {
  if (vertices.empty())
    return absl::InvalidArgumentError("Received empty set of verticies");

  UpdateColor(color);
  SDL_Point sdl_points[vertices.size()];
  for (int i = 0; i < vertices.size() + 1; i++) {
    const Point &point = vertices[i % vertices.size()];
    sdl_points[i] = {.x = static_cast<int>(point.x),
                     .y = static_cast<int>(point.y)};
    if (!static_position) sdl_points[i] = Adjust(&sdl_points[i]);
  }

  SDL_RenderDrawLines(renderer_, sdl_points, vertices.size() + 1);
  return absl::OkStatus();
}

void Camera::RenderGrid() {
  UpdateColor(DrawColor::kColorGrid);
  int x_adjustment = (config_->tiles.render_width() -
                      (rect_.x % config_->tiles.render_width()));
  for (std::vector<SDL_Point> line : grid_x_) {
    line[0].x += x_adjustment;
    line[1].x += x_adjustment;
    SDL_RenderDrawLine(renderer_, line[0].x, line[0].y, line[1].x, line[1].y);
  }
  int y_adjustment = (config_->tiles.render_height() -
                      (rect_.y % config_->tiles.render_height()));
  for (std::vector<SDL_Point> line : grid_y_) {
    line[0].y += y_adjustment;
    line[1].y += y_adjustment;
    SDL_RenderDrawLine(renderer_, line[0].x, line[0].y, line[1].x, line[1].y);
  }
}

}  // namespace zebes