#pragma once

#include "SDL.h"
#include "absl/status/statusor.h"
#include "common/sdl_wrapper.h"
#include "editor/preview_texture_sink.h"

namespace zebes {

// Holds one streaming texture and rewrites it in place.
//
// Generated artwork changes every time a control moves, so allocating a texture
// per upload would churn GPU memory for no reason. The texture is recreated
// only when the image changes size.
class SdlPreviewTexture : public PreviewTextureSink {
 public:
  explicit SdlPreviewTexture(SdlWrapper* sdl) : sdl_(sdl) {}
  ~SdlPreviewTexture() override;

  absl::StatusOr<ImTextureID> Upload(const RgbaImage& image) override;

 private:
  SdlWrapper* sdl_;
  SDL_Texture* texture_ = nullptr;
  int width_ = 0;
  int height_ = 0;
};

}  // namespace zebes
