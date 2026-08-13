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

absl::StatusOr<SDL_Texture*> SdlWrapper::CreateTextureFromPixels(int width, int height,
                                                                 const uint8_t* pixels) {
  if (!window_ || !renderer_) {
    return absl::FailedPreconditionError("SDL resources not initialized");
  }
  if (width <= 0 || height <= 0 || pixels == nullptr) {
    return absl::InvalidArgumentError("Cannot create a texture from an empty image");
  }

  SDL_Texture* texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888,
                                           SDL_TEXTUREACCESS_STREAMING, width, height);
  if (texture == nullptr) {
    return absl::InternalError(absl::StrCat("Failed to create texture: ", SDL_GetError()));
  }
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

  absl::Status updated = UpdateTexturePixels(texture, width, height, pixels);
  if (!updated.ok()) {
    SDL_DestroyTexture(texture);
    return updated;
  }
  return texture;
}

absl::Status SdlWrapper::UpdateTexturePixels(SDL_Texture* texture, int width, int height,
                                             const uint8_t* pixels) {
  if (texture == nullptr || pixels == nullptr) {
    return absl::InvalidArgumentError("Cannot update a null texture");
  }
  if (SDL_UpdateTexture(texture, nullptr, pixels, width * 4) != 0) {
    return absl::InternalError(absl::StrCat("Failed to update texture: ", SDL_GetError()));
  }
  return absl::OkStatus();
}

void SdlWrapper::DestroyTexture(SDL_Texture* texture) {
  if (texture) {
    SDL_DestroyTexture(texture);
  }
}

absl::StatusOr<std::unique_ptr<SdlWrapper>> SdlWrapper::Create(const WindowConfig& config) {
  uint32_t window_flags = 0;
  if (config.fullscreen) window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  if (config.resizable) window_flags |= SDL_WINDOW_RESIZABLE;
  if (config.high_dpi) window_flags |= SDL_WINDOW_ALLOW_HIGHDPI;

  const int window_x = config.centered ? SDL_WINDOWPOS_CENTERED : config.x;
  const int window_y = config.centered ? SDL_WINDOWPOS_CENTERED : config.y;
  SDL_Window* window = SDL_CreateWindow(config.title.c_str(), window_x, window_y, config.width,
                                        config.height,
                                        static_cast<SDL_WindowFlags>(window_flags));

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

int SdlWrapper::PollEvent(SDL_Event* event) { return SDL_PollEvent(event); }

const uint8_t* SdlWrapper::GetKeyboardState(int* numkeys) { return SDL_GetKeyboardState(numkeys); }

}  // namespace zebes
