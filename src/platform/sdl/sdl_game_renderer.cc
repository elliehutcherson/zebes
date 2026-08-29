#include "platform/sdl/sdl_game_renderer.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "SDL_error.h"
#include "SDL_render.h"
#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/sdl_wrapper.h"
#include "common/status_macros.h"
#include "engine/parallax_layout.h"
#include "engine/scene_composition.h"
#include "engine/scene_types.h"
#include "game/game_scene.h"
#include "objects/camera.h"
#include "objects/game_view.h"
#include "platform/sdl/sdl_texture_handle.h"

namespace zebes {
namespace {

struct NativeTextureInfo {
  SDL_Texture* texture = nullptr;
  int width = 0;
  int height = 0;
};

absl::StatusOr<NativeTextureInfo> ResolveTexture(TextureHandle handle) {
  SDL_Texture* texture = SdlTextureHandleAdapter::ToNative(handle);
  if (texture == nullptr) {
    return absl::FailedPreconditionError("Game scene texture handle cannot be resolved");
  }

  NativeTextureInfo info{.texture = texture};
  if (SDL_QueryTexture(texture, nullptr, nullptr, &info.width, &info.height) != 0 ||
      info.width <= 0 || info.height <= 0) {
    return absl::InternalError(absl::StrCat("Failed to query game texture: ", SDL_GetError()));
  }
  if (SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest) != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to configure game texture scaling: ", SDL_GetError()));
  }
  return info;
}

absl::Status ValidateSource(const PixelRect& source, const NativeTextureInfo& texture) {
  if (!source.IsValid() || source.width > texture.width || source.height > texture.height ||
      source.x > texture.width - source.width || source.y > texture.height - source.height) {
    return absl::InvalidArgumentError("Game texture source rectangle is out of bounds");
  }
  return absl::OkStatus();
}

absl::StatusOr<SDL_FRect> ScreenRect(const Camera& camera, const WorldRect& bounds) {
  if (!bounds.IsValid()) return absl::InvalidArgumentError("Game draw bounds are invalid");
  const Vec minimum = camera.WorldToScreen(bounds.min);
  const Vec maximum = camera.WorldToScreen(bounds.max);
  if (!std::isfinite(minimum.x) || !std::isfinite(minimum.y) || !std::isfinite(maximum.x) ||
      !std::isfinite(maximum.y) || maximum.x <= minimum.x || maximum.y <= minimum.y) {
    return absl::InvalidArgumentError("Game draw bounds produce invalid screen geometry");
  }
  return SDL_FRect{
      .x = static_cast<float>(minimum.x),
      .y = static_cast<float>(minimum.y),
      .w = static_cast<float>(maximum.x - minimum.x),
      .h = static_cast<float>(maximum.y - minimum.y),
  };
}

absl::Status DrawTexture(SDL_Renderer& renderer, const NativeTextureInfo& texture,
                         const std::optional<PixelRect>& source, const SDL_FRect& destination,
                         uint8_t opacity = 255) {
  uint8_t previous_opacity = 255;
  if (SDL_GetTextureAlphaMod(texture.texture, &previous_opacity) != 0 ||
      SDL_SetTextureAlphaMod(texture.texture, opacity) != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to configure game texture opacity: ", SDL_GetError()));
  }
  auto restore_opacity = absl::MakeCleanup(
      [&texture, previous_opacity] { SDL_SetTextureAlphaMod(texture.texture, previous_opacity); });

  SDL_Rect native_source;
  const SDL_Rect* native_source_pointer = nullptr;
  if (source.has_value()) {
    native_source = {
        .x = source->x,
        .y = source->y,
        .w = source->width,
        .h = source->height,
    };
    native_source_pointer = &native_source;
  }
  if (SDL_RenderCopyF(&renderer, texture.texture, native_source_pointer, &destination) != 0) {
    return absl::InternalError(absl::StrCat("Failed to draw game texture: ", SDL_GetError()));
  }
  return absl::OkStatus();
}

absl::Status RenderParallax(SDL_Renderer& renderer, const SceneParallaxRenderBatch& batch) {
  if (!std::isfinite(batch.opacity) || batch.opacity < 0.0 || batch.opacity > 1.0) {
    return absl::InvalidArgumentError("Game parallax opacity must be between zero and one");
  }
  const uint8_t opacity = static_cast<uint8_t>(std::lround(batch.opacity * 255.0));

  std::map<uint64_t, NativeTextureInfo> textures;
  for (const SceneParallaxRenderItem& item : batch.layers) {
    std::map<int, TextureHandle> element_textures;
    std::vector<ParallaxElementSize> element_sizes;
    element_sizes.reserve(item.elements.size());
    for (const SceneParallaxElementRenderResource& element : item.elements) {
      auto [texture, inserted] = textures.try_emplace(element.texture.id());
      if (inserted) {
        ASSIGN_OR_RETURN(texture->second, ResolveTexture(element.texture));
      }
      if (!element_textures.emplace(element.element_id, element.texture).second) {
        return absl::InvalidArgumentError("Game parallax layer has duplicate element IDs");
      }
      element_sizes.push_back({
          .element_id = element.element_id,
          .width = texture->second.width,
          .height = texture->second.height,
      });
    }

    ASSIGN_OR_RETURN(const ParallaxLayout layout,
                     CalculateParallaxLayout(batch.camera, item.layer, element_sizes));
    for (const ParallaxElementLayout& element : layout.elements) {
      auto handle = element_textures.find(element.element_id);
      if (handle == element_textures.end()) {
        return absl::FailedPreconditionError("Game parallax layout references an unbound element");
      }
      ASSIGN_OR_RETURN(const SDL_FRect destination, ScreenRect(batch.camera, element.bounds));
      RETURN_IF_ERROR(DrawTexture(renderer, textures.at(handle->second.id()), std::nullopt,
                                  destination, opacity));
    }
  }
  return absl::OkStatus();
}

absl::Status RenderTiles(SDL_Renderer& renderer, const Camera& camera,
                         const SceneTileRenderBatch& batch) {
  ASSIGN_OR_RETURN(const NativeTextureInfo texture, ResolveTexture(batch.atlas_texture));
  for (const SceneTileRenderItem& item : batch.items) {
    RETURN_IF_ERROR(ValidateSource(item.source, texture));
    ASSIGN_OR_RETURN(const SDL_FRect destination, ScreenRect(camera, item.bounds));
    RETURN_IF_ERROR(DrawTexture(renderer, texture, item.source, destination));
  }
  return absl::OkStatus();
}

absl::Status RenderEntities(SDL_Renderer& renderer, const Camera& camera,
                            const std::vector<SceneEntityRenderItem>& items) {
  std::map<uint64_t, NativeTextureInfo> textures;
  for (const SceneEntityRenderItem& item : items) {
    ASSIGN_OR_RETURN(const SDL_FRect destination, ScreenRect(camera, item.bounds));
    if (!item.sprite.has_value()) {
      if (SDL_SetRenderDrawColor(&renderer, 100, 100, 200, 180) != 0 ||
          SDL_RenderFillRectF(&renderer, &destination) != 0) {
        return absl::InternalError(
            absl::StrCat("Failed to draw game entity placeholder: ", SDL_GetError()));
      }
      continue;
    }

    auto [texture, inserted] = textures.try_emplace(item.sprite->texture.id());
    if (inserted) {
      ASSIGN_OR_RETURN(texture->second, ResolveTexture(item.sprite->texture));
    }
    RETURN_IF_ERROR(ValidateSource(item.sprite->source, texture->second));
    RETURN_IF_ERROR(DrawTexture(renderer, texture->second, item.sprite->source, destination));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<SdlGameRenderer>> SdlGameRenderer::Create(SdlWrapper& sdl,
                                                                         GameViewSize game_view) {
  if (!game_view.IsValid()) return absl::InvalidArgumentError("Game view is invalid");
  SDL_Renderer* renderer = sdl.GetRenderer();
  if (renderer == nullptr) return absl::FailedPreconditionError("SDL renderer is unavailable");
  if (SDL_RenderSetLogicalSize(renderer, game_view.width, game_view.height) != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to configure logical game view: ", SDL_GetError()));
  }
  if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) != 0) {
    return absl::InternalError(absl::StrCat("Failed to configure game blending: ", SDL_GetError()));
  }
  return std::unique_ptr<SdlGameRenderer>(new SdlGameRenderer(*renderer, game_view));
}

SdlGameRenderer::SdlGameRenderer(SDL_Renderer& renderer, GameViewSize game_view)
    : renderer_(renderer), game_view_(game_view) {}

absl::Status SdlGameRenderer::Render(const GameSceneFrame& frame) const {
  RETURN_IF_ERROR(ValidateSceneCamera(frame.camera));
  if (frame.camera.viewport_width != game_view_.width ||
      frame.camera.viewport_height != game_view_.height) {
    return absl::InvalidArgumentError("Game scene camera does not match the logical game view");
  }
  if (SDL_SetRenderDrawColor(&renderer_, 8, 8, 12, 255) != 0 || SDL_RenderClear(&renderer_) != 0) {
    return absl::InternalError(absl::StrCat("Failed to clear game frame: ", SDL_GetError()));
  }
  for (const SceneParallaxRenderBatch& batch : frame.parallax) {
    RETURN_IF_ERROR(RenderParallax(renderer_, batch));
  }
  for (const GameWorldLayerFrame& layer : frame.world_layers) {
    RETURN_IF_ERROR(RenderTiles(renderer_, frame.camera, layer.tiles));
    RETURN_IF_ERROR(RenderEntities(renderer_, frame.camera, layer.entities));
  }
  SDL_RenderPresent(&renderer_);
  return absl::OkStatus();
}

}  // namespace zebes
