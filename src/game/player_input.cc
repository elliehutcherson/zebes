#include "game/player_input.h"

#include "engine/input_types.h"

namespace zebes {

PlayerInputIntent BuildPlayerInputIntent(const InputSnapshot& current,
                                         const InputSnapshot& previous) {
  const bool move_left = current.IsKeyDown(Key::kA);
  const bool move_right = current.IsKeyDown(Key::kD);
  const bool jump_held = current.IsKeyDown(Key::kSpace);
  return {
      .horizontal_axis = static_cast<int>(move_right) - static_cast<int>(move_left),
      .jump_pressed = jump_held && !previous.IsKeyDown(Key::kSpace),
      .jump_held = jump_held,
  };
}

}  // namespace zebes
