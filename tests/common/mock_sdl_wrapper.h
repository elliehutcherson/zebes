#pragma once

#include "common/sdl_wrapper.h"
#include "gmock/gmock.h"

namespace zebes {

class MockSdlWrapper : public SdlWrapper {
 public:
  // Use a constructor that passes nulls to the base class since we mock everything
  MockSdlWrapper() : SdlWrapper(nullptr, nullptr) {}

  MOCK_METHOD(int, PollEvent, (SDL_Event * event), (override));
  MOCK_METHOD(const uint8_t*, GetKeyboardState, (int* numkeys), (override));
  MOCK_METHOD(absl::StatusOr<SDL_Texture*>, CreateTexture, (const std::string& path), (override));
  MOCK_METHOD(void, DestroyTexture, (SDL_Texture * texture), (override));
};

}  // namespace zebes
