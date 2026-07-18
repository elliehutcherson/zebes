#include "platform/sdl/sdl_input_source.h"

#include <cstdint>
#include <vector>

#include "SDL.h"
#include "common/mock_imgui_wrapper.h"
#include "common/mock_sdl_wrapper.h"
#include "engine/input_types.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::Return;

TEST(SdlInputSourceTest, TranslatesKeyboardStateToEngineKeys) {
  MockSdlWrapper sdl;
  MockImGuiWrapper imgui;
  SdlInputSource input_source(sdl, imgui);
  std::vector<uint8_t> keyboard_state(SDL_NUM_SCANCODES, 0);
  keyboard_state[SDL_SCANCODE_W] = 1;
  keyboard_state[SDL_SCANCODE_SPACE] = 1;

  EXPECT_CALL(sdl, PollEvent(_)).WillOnce(Return(0));
  EXPECT_CALL(sdl, GetKeyboardState(_)).WillOnce(Return(keyboard_state.data()));

  InputSnapshot snapshot = input_source.Poll();

  EXPECT_TRUE(snapshot.IsKeyDown(Key::kW));
  EXPECT_TRUE(snapshot.IsKeyDown(Key::kSpace));
  EXPECT_FALSE(snapshot.IsKeyDown(Key::kA));
}

TEST(SdlInputSourceTest, ForwardsEventsAndTranslatesWindowClose) {
  MockSdlWrapper sdl;
  MockImGuiWrapper imgui;
  SdlInputSource input_source(sdl, imgui);
  std::vector<uint8_t> keyboard_state(SDL_NUM_SCANCODES, 0);

  EXPECT_CALL(sdl, PollEvent(_))
      .WillOnce([](SDL_Event* event) {
        event->type = SDL_WINDOWEVENT;
        event->window.event = SDL_WINDOWEVENT_CLOSE;
        return 1;
      })
      .WillOnce(Return(0));
  EXPECT_CALL(imgui, ProcessEvent(_)).WillOnce(Return(true));
  EXPECT_CALL(sdl, GetKeyboardState(_)).WillOnce(Return(keyboard_state.data()));

  EXPECT_TRUE(input_source.Poll().quit_requested);
}

}  // namespace
}  // namespace zebes
