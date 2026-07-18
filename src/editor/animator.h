#pragma once

#include <vector>

#include "absl/status/statusor.h"
#include "objects/sprite.h"

namespace zebes {

class Animator {
 public:
  Animator() = default;
  ~Animator() = default;

  // Reset playback to the first frame.
  void Reset();

  // Advance the supplied live frame collection by one tick.
  void Update(const std::vector<SpriteFrame>& frames);

  // Get the current frame of the animation.
  // Returns an error when frames are empty.
  absl::StatusOr<SpriteFrame> GetCurrentFrame(const std::vector<SpriteFrame>& frames) const;
  absl::StatusOr<int> GetCurrentFrameIndex(const std::vector<SpriteFrame>& frames) const;

  bool IsActive(const std::vector<SpriteFrame>& frames) const;

 private:
  int current_frame_index_ = 0;
  int tick_counter_ = 0;
};

}  // namespace zebes
