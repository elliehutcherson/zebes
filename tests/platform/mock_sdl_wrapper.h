#pragma once

#include "gmock/gmock.h"
#include "platform/sdl/sdl_wrapper.h"

namespace zebes {

class MockSdlWrapper : public SdlWrapper {
 public:
  // Use a constructor that passes nulls to the base class since we mock everything
  MockSdlWrapper() : SdlWrapper(nullptr, nullptr) {}

  MOCK_METHOD(int, PollEvent, (SDL_Event * event), (override));
  MOCK_METHOD(const uint8_t*, GetKeyboardState, (int* numkeys), (override));
  MOCK_METHOD(absl::StatusOr<SDL_Texture*>, CreateTexture, (const std::string& path), (override));
  MOCK_METHOD(absl::StatusOr<SDL_Texture*>, CreateTextureFromPixels,
              (int width, int height, const uint8_t* pixels), (override));
  MOCK_METHOD(absl::Status, UpdateTexturePixels,
              (SDL_Texture * texture, int width, int height, const uint8_t* pixels), (override));
  MOCK_METHOD(void, DestroyTexture, (SDL_Texture * texture), (override));
};

}  // namespace zebes
