#include "editor/sdl_preview_texture.h"

#include "absl/status/status.h"
#include "common/status_macros.h"

namespace zebes {

SdlPreviewTexture::~SdlPreviewTexture() {
  if (texture_ != nullptr) sdl_->DestroyTexture(texture_);
}

absl::StatusOr<ImTextureID> SdlPreviewTexture::Upload(const RgbaImage& image) {
  if (!image.IsValid()) {
    return absl::InvalidArgumentError("Cannot upload a malformed preview image");
  }

  if (texture_ == nullptr || image.width != width_ || image.height != height_) {
    if (texture_ != nullptr) sdl_->DestroyTexture(texture_);
    ASSIGN_OR_RETURN(texture_, sdl_->CreateTextureFromPixels(image.width, image.height,
                                                             image.pixels.data()));
    width_ = image.width;
    height_ = image.height;
    return reinterpret_cast<ImTextureID>(texture_);
  }

  RETURN_IF_ERROR(
      sdl_->UpdateTexturePixels(texture_, image.width, image.height, image.pixels.data()));
  return reinterpret_cast<ImTextureID>(texture_);
}

}  // namespace zebes
