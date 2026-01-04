#pragma once

#include "engine/input_manager_interface.h"
#include "gmock/gmock.h"

namespace zebes {

class MockInputManager : public IInputManager {
 public:
  MockInputManager() = default;

  MOCK_METHOD(void, BindAction, (absl::string_view action_name, SDL_Scancode key), (override));
  MOCK_METHOD(void, Update, (), (override));
  MOCK_METHOD(bool, IsActionActive, (absl::string_view action_name), (const, override));
  MOCK_METHOD(bool, IsActionJustPressed, (absl::string_view action_name), (const, override));
  MOCK_METHOD(bool, QuitRequested, (), (const, override));
};

}  // namespace zebes
