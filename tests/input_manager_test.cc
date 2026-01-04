#include "engine/input_manager.h"

#include <memory>
#include <string>
#include <vector>

#include "SDL_scancode.h"
#include "common/mock_sdl_wrapper.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;

class InputManagerTest : public ::testing::Test {
 protected:
  void SetUp() override { mock_sdl_ = std::make_unique<MockSdlWrapper>(); }

  void CreateManager() {
    InputManager::Options options;
    options.sdl_wrapper = mock_sdl_.get();
    auto manager_or = InputManager::Create(std::move(options));
    ASSERT_TRUE(manager_or.ok());
    input_manager_ = std::move(manager_or.value());
  }

  std::unique_ptr<MockSdlWrapper> mock_sdl_;
  std::unique_ptr<InputManager> input_manager_;
};

TEST_F(InputManagerTest, CreateFailsWithNullWrapper) {
  InputManager::Options options;
  options.sdl_wrapper = nullptr;
  auto result = InputManager::Create(std::move(options));
  EXPECT_FALSE(result.ok());
}

TEST_F(InputManagerTest, BindActionAndQueryInactive) {
  CreateManager();
  input_manager_->BindAction("jump", SDL_SCANCODE_SPACE);

  // No update called, keyboard state should be empty/zero
  EXPECT_FALSE(input_manager_->IsActionActive("jump"));
}

TEST_F(InputManagerTest, ActionActiveWhenKeyPressed) {
  CreateManager();
  input_manager_->BindAction("move_right", SDL_SCANCODE_RIGHT);

  // Setup mock behavior
  std::vector<uint8_t> mock_state(SDL_NUM_SCANCODES, 0);
  mock_state[SDL_SCANCODE_RIGHT] = 1;

  EXPECT_CALL(*mock_sdl_, GetKeyboardState(_)).WillRepeatedly(Return(mock_state.data()));
  EXPECT_CALL(*mock_sdl_, PollEvent(_)).WillRepeatedly(Return(0));

  // Update to read the state
  input_manager_->Update();

  EXPECT_TRUE(input_manager_->IsActionActive("move_right"));
  EXPECT_FALSE(input_manager_->IsActionActive("jump"));
}

TEST_F(InputManagerTest, ActionJustPressedLogic) {
  CreateManager();
  input_manager_->BindAction("fire", SDL_SCANCODE_F);

  std::vector<uint8_t> released_state(SDL_NUM_SCANCODES, 0);
  std::vector<uint8_t> pressed_state(SDL_NUM_SCANCODES, 0);
  pressed_state[SDL_SCANCODE_F] = 1;

  // 1. Initial State: Key Not Pressed
  // We need to sequence these calls
  {
    InSequence s;

    // Update 1: Not pressed
    EXPECT_CALL(*mock_sdl_, PollEvent(_)).WillOnce(Return(0));
    EXPECT_CALL(*mock_sdl_, GetKeyboardState(_)).WillOnce(Return(released_state.data()));

    // Update 2: Pressed
    EXPECT_CALL(*mock_sdl_, PollEvent(_)).WillOnce(Return(0));
    EXPECT_CALL(*mock_sdl_, GetKeyboardState(_)).WillOnce(Return(pressed_state.data()));

    // Update 3: Held
    EXPECT_CALL(*mock_sdl_, PollEvent(_)).WillOnce(Return(0));
    EXPECT_CALL(*mock_sdl_, GetKeyboardState(_)).WillOnce(Return(pressed_state.data()));

    // Update 4: Released
    EXPECT_CALL(*mock_sdl_, PollEvent(_)).WillOnce(Return(0));
    EXPECT_CALL(*mock_sdl_, GetKeyboardState(_)).WillOnce(Return(released_state.data()));
  }

  // 1.
  input_manager_->Update();
  EXPECT_FALSE(input_manager_->IsActionJustPressed("fire"));

  // 2. Press Key
  input_manager_->Update();
  EXPECT_TRUE(input_manager_->IsActionJustPressed("fire"));
  EXPECT_TRUE(input_manager_->IsActionActive("fire"));

  // 3. Hold Key
  input_manager_->Update();
  EXPECT_TRUE(input_manager_->IsActionActive("fire"));
  EXPECT_FALSE(input_manager_->IsActionJustPressed("fire"));

  // 4. Release Key
  input_manager_->Update();
  EXPECT_FALSE(input_manager_->IsActionActive("fire"));
  EXPECT_FALSE(input_manager_->IsActionJustPressed("fire"));
}

}  // namespace
}  // namespace zebes
