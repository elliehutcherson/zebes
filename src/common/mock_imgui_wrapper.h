#pragma once

#include "common/imgui_wrapper.h"
#include "gmock/gmock.h"

namespace zebes {

class MockImGuiWrapper : public ImGuiWrapper {
 public:
  MOCK_METHOD(bool, ProcessEvent, (const SDL_Event* event), (override));
};

}  // namespace zebes
