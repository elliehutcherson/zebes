#pragma once

#include <vector>

#include "absl/status/statusor.h"
#include "objects/sprite.h"

namespace zebes {

// A platform-neutral playback cursor for a sprite animation. The cursor owns
// only transient playback state; callers provide the current frame sequence on
// every operation so an authored sprite can change while it is being previewed.
// One Update() advances one simulation tick. Each frame lasts its authored
// frames_per_cycle ticks, with zero treated as one tick.
class AnimationCursor {
 public:
  AnimationCursor() = default;
  ~AnimationCursor() = default;

  void Reset();

  void Update(const std::vector<SpriteFrame>& frames);

  // Fails when frames is empty.
  absl::StatusOr<SpriteFrame> GetCurrentFrame(const std::vector<SpriteFrame>& frames) const;
  absl::StatusOr<int> GetCurrentFrameIndex(const std::vector<SpriteFrame>& frames) const;

  static bool IsActive(const std::vector<SpriteFrame>& frames);

 private:
  int current_frame_index_ = 0;
  int tick_counter_ = 0;
};

}  // namespace zebes
