#pragma once

#include <memory>

#include "SDL.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/config.h"

namespace zebes {

class SdlWrapper {
 public:
  static absl::StatusOr<std::unique_ptr<SdlWrapper>> Create(const WindowConfig& config);

  virtual ~SdlWrapper();

  // Window Control
  absl::Status SetWindowFullscreen(bool fullscreen);
  absl::Status SetWindowResizable(bool resizable);
  absl::Status SetWindowTitle(const std::string& title);

  // Input & Events
  virtual int PollEvent(SDL_Event* event);
  virtual const uint8_t* GetKeyboardState(int* numkeys);

  // Texture Management
  virtual absl::StatusOr<SDL_Texture*> CreateTexture(const std::string& path);

  // Creates a texture from pixels held in memory, for artwork that has no file
  // behind it. A live preview regenerates its image every time a control moves,
  // so round-tripping it through disk would be both slow and litter.
  //
  // The texture is created streaming so the same one can be rewritten in place;
  // see UpdateTexturePixels.
  virtual absl::StatusOr<SDL_Texture*> CreateTextureFromPixels(int width, int height,
                                                               const uint8_t* pixels);

  // Rewrites a texture created by CreateTextureFromPixels. Sizes must match.
  virtual absl::Status UpdateTexturePixels(SDL_Texture* texture, int width, int height,
                                           const uint8_t* pixels);

  virtual void DestroyTexture(SDL_Texture* texture);

  // Resource Access
  // Exposed for ImGui and legacy rendering code.
  // Prefer using wrapper methods where available.
  SDL_Window* GetWindow() const { return window_; }
  SDL_Renderer* GetRenderer() const { return renderer_; }

 protected:
  explicit SdlWrapper(SDL_Window* window, SDL_Renderer* renderer);

 private:
  SDL_Window* window_;
  SDL_Renderer* renderer_;
};

}  // namespace zebes
