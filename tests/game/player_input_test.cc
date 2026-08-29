#include "game/player_input.h"

#include "engine/input_types.h"
#include "gtest/gtest.h"

namespace zebes {
namespace {

TEST(PlayerInputTest, DerivesHorizontalAxisAndCancelsOpposingInput) {
  InputSnapshot input;
  input.SetKeyDown(Key::kA);
  EXPECT_EQ(BuildPlayerInputIntent(input, {}).horizontal_axis, -1);

  input.SetKeyDown(Key::kD);
  EXPECT_EQ(BuildPlayerInputIntent(input, {}).horizontal_axis, 0);

  input.SetKeyDown(Key::kA, false);
  EXPECT_EQ(BuildPlayerInputIntent(input, {}).horizontal_axis, 1);
}

TEST(PlayerInputTest, EmitsJumpEdgeOnlyOnPress) {
  InputSnapshot released;
  InputSnapshot pressed;
  pressed.SetKeyDown(Key::kSpace);

  const PlayerInputIntent first_tick = BuildPlayerInputIntent(pressed, released);
  EXPECT_TRUE(first_tick.jump_pressed);
  EXPECT_TRUE(first_tick.jump_held);

  const PlayerInputIntent catch_up_tick = BuildPlayerInputIntent(pressed, pressed);
  EXPECT_FALSE(catch_up_tick.jump_pressed);
  EXPECT_TRUE(catch_up_tick.jump_held);

  EXPECT_TRUE(BuildPlayerInputIntent(pressed, released).jump_pressed);
}

}  // namespace
}  // namespace zebes
