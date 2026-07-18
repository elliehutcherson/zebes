#include "engine/input_manager.h"

#include <memory>

#include "engine/input_types.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace zebes {
namespace {

using ::testing::Return;

class MockInputSource : public InputSource {
 public:
  MOCK_METHOD(InputSnapshot, Poll, (), (override));
};

class InputManagerTest : public ::testing::Test {
 protected:
  void CreateManager() {
    absl::StatusOr<std::unique_ptr<InputManager>> manager_or =
        InputManager::Create({.input_source = &input_source_});
    ASSERT_TRUE(manager_or.ok());
    input_manager_ = std::move(manager_or.value());
  }

  MockInputSource input_source_;
  std::unique_ptr<InputManager> input_manager_;
};

TEST_F(InputManagerTest, CreateFailsWithNullInputSource) {
  absl::StatusOr<std::unique_ptr<InputManager>> result =
      InputManager::Create({.input_source = nullptr});
  EXPECT_FALSE(result.ok());
}

TEST_F(InputManagerTest, BindActionAndQueryInactive) {
  CreateManager();
  input_manager_->BindAction("jump", Key::kSpace);

  EXPECT_FALSE(input_manager_->IsActionActive("jump"));
}

TEST_F(InputManagerTest, ActionActiveWhenKeyPressed) {
  CreateManager();
  input_manager_->BindAction("move_right", Key::kRight);

  InputSnapshot snapshot;
  snapshot.SetKeyDown(Key::kRight);
  EXPECT_CALL(input_source_, Poll()).WillOnce(Return(snapshot));

  input_manager_->Update();

  EXPECT_TRUE(input_manager_->IsActionActive("move_right"));
  EXPECT_FALSE(input_manager_->IsActionActive("jump"));
}

TEST_F(InputManagerTest, ActionJustPressedLogic) {
  CreateManager();
  input_manager_->BindAction("fire", Key::kF);

  InputSnapshot released;
  InputSnapshot pressed;
  pressed.SetKeyDown(Key::kF);
  EXPECT_CALL(input_source_, Poll())
      .WillOnce(Return(released))
      .WillOnce(Return(pressed))
      .WillOnce(Return(pressed))
      .WillOnce(Return(released));

  input_manager_->Update();
  EXPECT_FALSE(input_manager_->IsActionJustPressed("fire"));

  input_manager_->Update();
  EXPECT_TRUE(input_manager_->IsActionJustPressed("fire"));
  EXPECT_TRUE(input_manager_->IsActionActive("fire"));

  input_manager_->Update();
  EXPECT_TRUE(input_manager_->IsActionActive("fire"));
  EXPECT_FALSE(input_manager_->IsActionJustPressed("fire"));

  input_manager_->Update();
  EXPECT_FALSE(input_manager_->IsActionActive("fire"));
  EXPECT_FALSE(input_manager_->IsActionJustPressed("fire"));
}

TEST_F(InputManagerTest, QuitRequestIsLatched) {
  CreateManager();

  InputSnapshot quitting;
  quitting.quit_requested = true;
  EXPECT_CALL(input_source_, Poll()).WillOnce(Return(quitting)).WillOnce(Return(InputSnapshot{}));

  input_manager_->Update();
  EXPECT_TRUE(input_manager_->QuitRequested());

  input_manager_->Update();
  EXPECT_TRUE(input_manager_->QuitRequested());
}

}  // namespace
}  // namespace zebes
