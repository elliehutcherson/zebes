#include "editor/texture_preview.h"

#include <algorithm>
#include <cmath>

#include "SDL_error.h"
#include "SDL_render.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "platform/sdl/sdl_texture_handle.h"

namespace zebes {

absl::StatusOr<TexturePreviewLayout> CalculateTexturePreviewLayout(int source_width,
                                                                   int source_height,
                                                                   float max_width,
                                                                   float max_height) {
  if (source_width <= 0 || source_height <= 0) {
    return absl::InvalidArgumentError("texture preview source dimensions must be positive");
  }
  if (!std::isfinite(max_width) || !std::isfinite(max_height) || max_width <= 0.0f ||
      max_height <= 0.0f) {
    return absl::InvalidArgumentError("texture preview bounds must be finite and positive");
  }

  const float scale = std::min(max_width / static_cast<float>(source_width),
                               max_height / static_cast<float>(source_height));
  return TexturePreviewLayout{
      .source_width = source_width,
      .source_height = source_height,
      .display_width = source_width * scale,
      .display_height = source_height * scale,
  };
}

absl::Status FrameImagePreviewCamera(Camera& camera, int source_width, int source_height,
                                     float fill_fraction, CameraZoomRange zoom_range) {
  if (camera.viewport_width <= 0 || camera.viewport_height <= 0) {
    return absl::InvalidArgumentError("image preview camera requires a positive viewport");
  }
  if (!std::isfinite(fill_fraction) || fill_fraction <= 0.0f || fill_fraction > 1.0f) {
    return absl::InvalidArgumentError("image preview fill fraction must be in (0, 1]");
  }
  if (!zoom_range.IsValid()) {
    return absl::InvalidArgumentError("image preview camera requires a valid zoom range");
  }
  absl::StatusOr<TexturePreviewLayout> layout = CalculateTexturePreviewLayout(
      source_width, source_height, static_cast<float>(camera.viewport_width) * fill_fraction,
      static_cast<float>(camera.viewport_height) * fill_fraction);
  if (!layout.ok()) return layout.status();
  camera.position = Vec{source_width / 2.0, source_height / 2.0};
  camera.zoom = zoom_range.Clamp(layout->display_width / source_width);
  return absl::OkStatus();
}

absl::StatusOr<AtlasBinding> TexturePreviewRenderer::BindAtlas(TextureHandle texture) {
  if (!texture) return AtlasBinding{};

  SDL_Texture* native_texture = SdlTextureHandleAdapter::ToNative(texture);
  if (native_texture == nullptr) return AtlasBinding{};

  int width = 0;
  int height = 0;
  if (SDL_QueryTexture(native_texture, nullptr, nullptr, &width, &height) != 0) {
    return absl::InternalError(absl::StrCat("failed to query atlas texture: ", SDL_GetError()));
  }

  return AtlasBinding{
      .texture_id = reinterpret_cast<ImTextureID>(native_texture),
      .width = width,
      .height = height,
  };
}

absl::StatusOr<TexturePreviewLayout> TexturePreviewRenderer::Render(TextureHandle texture,
                                                                    float max_width,
                                                                    float max_height) const {
  if (!texture) {
    return absl::FailedPreconditionError("texture preview requires a managed texture");
  }

  SDL_Texture* native_texture = SdlTextureHandleAdapter::ToNative(texture);
  if (native_texture == nullptr) {
    return absl::FailedPreconditionError("texture preview handle cannot be resolved");
  }

  int width = 0;
  int height = 0;
  if (SDL_QueryTexture(native_texture, nullptr, nullptr, &width, &height) != 0) {
    return absl::InternalError(absl::StrCat("failed to query preview texture: ", SDL_GetError()));
  }

  absl::StatusOr<TexturePreviewLayout> layout =
      CalculateTexturePreviewLayout(width, height, max_width, max_height);
  if (!layout.ok()) return layout.status();
  gui_.Image(reinterpret_cast<ImTextureID>(native_texture),
             ImVec2(layout->display_width, layout->display_height));
  return *layout;
}

}  // namespace zebes
