#include "common/sdl_wrapper.h"

#include "SDL_image.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"

namespace zebes {

absl::StatusOr<SDL_Texture*> SdlWrapper::CreateTexture(const std::string& path) {
  if (!window_ || !renderer_) {
    return absl::FailedPreconditionError("SDL resources not initialized");
  }

  SDL_Surface* surface = IMG_Load(path.c_str());
  if (surface == nullptr) {
    return absl::InternalError(absl::StrCat("Failed to load image: ", IMG_GetError()));
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
  SDL_FreeSurface(surface);

  if (texture == nullptr) {
    return absl::InternalError(
        absl::StrCat("Failed to create texture from surface: ", SDL_GetError()));
  }

  return texture;
}

void SdlWrapper::DestroyTexture(SDL_Texture* texture) {
  if (texture) {
    SDL_DestroyTexture(texture);
  }
}

absl::StatusOr<std::unique_ptr<SdlWrapper>> SdlWrapper::Create(const WindowConfig& config) {
  // Ensure editor is always resizable and high-dpi aware, while respecting config flags
  // We use the config flags as a base, but enforce RESIZABLE/HIGHDPI for the editor context
  // unless specifically overridden by the user config implies we shouldn't (but here we force it
  // for editor usability).
  uint32_t window_flags = config.flags | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

  SDL_Window* window = SDL_CreateWindow(config.title.c_str(), config.xpos, config.ypos,
                                        config.width, config.height, (SDL_WindowFlags)window_flags);

  if (window == nullptr) {
    return absl::InternalError(absl::StrCat("Failed to create SDL window: ", SDL_GetError()));
  }

  SDL_Renderer* renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
  if (renderer == nullptr) {
    SDL_DestroyWindow(window);
    return absl::InternalError(absl::StrCat("Failed to create SDL renderer: ", SDL_GetError()));
  }

  return absl::WrapUnique(new SdlWrapper(window, renderer));
}

SdlWrapper::SdlWrapper(SDL_Window* window, SDL_Renderer* renderer)
    : window_(window), renderer_(renderer) {}

SdlWrapper::~SdlWrapper() {
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
  }
  if (window_) {
    SDL_DestroyWindow(window_);
  }
}

absl::Status SdlWrapper::SetWindowFullscreen(bool fullscreen) {
  if (!window_) return absl::FailedPreconditionError("Window is null");

  if (SDL_SetWindowFullscreen(window_, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
    return absl::InternalError(absl::StrCat("Failed to set fullscreen: ", SDL_GetError()));
  }
  return absl::OkStatus();
}

absl::Status SdlWrapper::SetWindowResizable(bool resizable) {
  if (!window_) return absl::FailedPreconditionError("Window is null");
  SDL_SetWindowResizable(window_, resizable ? SDL_TRUE : SDL_FALSE);
  return absl::OkStatus();
}

absl::Status SdlWrapper::SetWindowTitle(const std::string& title) {
  if (!window_) return absl::FailedPreconditionError("Window is null");
  SDL_SetWindowTitle(window_, title.c_str());
  return absl::OkStatus();
}

}  // namespace zebes
