#include "engine/animation.h"

#include <algorithm>

#include "absl/status/status.h"
#include "common/status_macros.h"

namespace zebes {

void AnimationCursor::Reset() {
  current_frame_index_ = 0;
  tick_counter_ = 0;
}

void AnimationCursor::Update(const std::vector<SpriteFrame>& frames) {
  if (frames.empty()) return;
  if (current_frame_index_ >= static_cast<int>(frames.size())) Reset();

  const SpriteFrame& current_frame = frames[current_frame_index_];

  tick_counter_++;

  // A frame declaring zero ticks would never advance and the animation would
  // freeze on it, so treat it as one.
  if (tick_counter_ >= std::max(1, current_frame.frames_per_cycle)) {
    tick_counter_ = 0;
    current_frame_index_++;

    if (current_frame_index_ >= static_cast<int>(frames.size())) {
      current_frame_index_ = 0;
    }
  }
}

absl::StatusOr<SpriteFrame> AnimationCursor::GetCurrentFrame(
    const std::vector<SpriteFrame>& frames) const {
  ASSIGN_OR_RETURN(const int current_index, GetCurrentFrameIndex(frames));
  return frames[current_index];
}

absl::StatusOr<int> AnimationCursor::GetCurrentFrameIndex(
    const std::vector<SpriteFrame>& frames) const {
  if (frames.empty()) return absl::FailedPreconditionError("Animation has no frames.");
  return current_frame_index_ % static_cast<int>(frames.size());
}

bool AnimationCursor::IsActive(const std::vector<SpriteFrame>& frames) { return !frames.empty(); }

}  // namespace zebes
