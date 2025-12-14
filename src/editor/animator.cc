#include "editor/animator.h"

#include "absl/status/status.h"

namespace zebes {

void Animator::SetSprite(const Sprite& sprite) {
  sprite_ = sprite;
  current_frame_index_ = 0;
  tick_counter_ = 0;
}

void Animator::Update() {
  if (!sprite_.has_value() || sprite_->frames.empty()) {
    return;
  }

  const SpriteFrame& current_frame = sprite_->frames[current_frame_index_];

  // Increment tick counter
  tick_counter_++;

  // Check if we need to advance to the next frame
  if (tick_counter_ >= current_frame.frames_per_cycle) {
    tick_counter_ = 0;
    current_frame_index_++;

    // Loop back to start if we reached the end
    if (current_frame_index_ >= sprite_->frames.size()) {
      current_frame_index_ = 0;
    }
  }
}

absl::StatusOr<SpriteFrame> Animator::GetCurrentFrame() const {
  if (!sprite_.has_value()) {
    return absl::FailedPreconditionError("No sprite meta set.");
  }
  if (sprite_->frames.empty()) {
    return absl::FailedPreconditionError("Sprite meta has no frames.");
  }
  if (current_frame_index_ < 0 || current_frame_index_ >= sprite_->frames.size()) {
    return absl::InternalError("Current frame index out of bounds.");
  }

  return sprite_->frames[current_frame_index_];
}

bool Animator::IsActive() const { return sprite_.has_value() && !sprite_->frames.empty(); }

}  // namespace zebes
