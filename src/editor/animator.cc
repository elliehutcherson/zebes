#include "editor/animator.h"

#include <algorithm>

#include "absl/status/status.h"

namespace zebes {

void Animator::Reset() {
  current_frame_index_ = 0;
  tick_counter_ = 0;
}

void Animator::Update(const std::vector<SpriteFrame>& frames) {
  if (frames.empty()) return;
  if (current_frame_index_ >= static_cast<int>(frames.size())) Reset();

  const SpriteFrame& current_frame = frames[current_frame_index_];

  // Increment tick counter
  tick_counter_++;

  // Check if we need to advance to the next frame
  if (tick_counter_ >= std::max(1, current_frame.frames_per_cycle)) {
    tick_counter_ = 0;
    current_frame_index_++;

    // Loop back to start if we reached the end
    if (current_frame_index_ >= static_cast<int>(frames.size())) {
      current_frame_index_ = 0;
    }
  }
}

absl::StatusOr<SpriteFrame> Animator::GetCurrentFrame(
    const std::vector<SpriteFrame>& frames) const {
  absl::StatusOr<int> current_index = GetCurrentFrameIndex(frames);
  if (!current_index.ok()) return current_index.status();
  return frames[*current_index];
}

absl::StatusOr<int> Animator::GetCurrentFrameIndex(const std::vector<SpriteFrame>& frames) const {
  if (frames.empty()) return absl::FailedPreconditionError("Animation has no frames.");
  return current_frame_index_ % static_cast<int>(frames.size());
}

bool Animator::IsActive(const std::vector<SpriteFrame>& frames) const { return !frames.empty(); }

}  // namespace zebes
