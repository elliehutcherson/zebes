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
