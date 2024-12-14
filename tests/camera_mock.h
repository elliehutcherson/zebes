#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "SDL_pixels.h"
#include "SDL_rect.h"
#include "SDL_render.h"
#include "SDL_ttf.h"
#include "absl/status/status.h"
#include "engine/vector.h"

namespace zebes {

class CameraMock : public CameraInterface {
 public:
  MOCK_METHOD(void, Update, (int center_x, int center_y), (override));
  MOCK_METHOD(void, Render,
              (SDL_Texture * texture, const SDL_Rect *src_rect,
               const SDL_Rect *dst_rect),
              (const, override));
  MOCK_METHOD(void, RenderStatic,
              (SDL_Texture * texture, SDL_Rect *src_rect, SDL_Rect *dst_rect),
              (const, override));
  MOCK_METHOD(void, RenderPlayerRectangle, (SDL_Rect * dst_rect), (override));
  MOCK_METHOD(void, RenderTileRectangle, (SDL_Rect * dst_rect), (override));
  MOCK_METHOD(void, RenderMenuRectangle, (SDL_Rect * dst_rect), (override));
  MOCK_METHOD(void, RenderText,
              (const std::string &message, SDL_Rect *dst_rect),
              (const, override));
  MOCK_METHOD(absl::Status, RenderLines,
              (const std::vector<Point> &vertices, DrawColor color,
               bool static_position),
              (override));
  MOCK_METHOD(void, RenderGrid, (), (override));
};

}  // namespace zebes