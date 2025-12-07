#include "editor/animator.h"

#include "absl/status/status.h"

namespace zebes {

void Animator::SetSpriteConfig(const SpriteConfig& config) {
  config_ = config;
  current_frame_index_ = 0;
  tick_counter_ = 0;
}

void Animator::Update() {
  if (!config_.has_value() || config_->sprite_frames.empty()) {
    return;
  }

  const SpriteFrame& current_frame = config_->sprite_frames[current_frame_index_];

  // Increment tick counter
  tick_counter_++;

  // Check if we need to advance to the next frame
  if (tick_counter_ >= current_frame.frames_per_cycle) {
    tick_counter_ = 0;
    current_frame_index_++;

    // Loop back to start if we reached the end
    if (current_frame_index_ >= config_->sprite_frames.size()) {
      current_frame_index_ = 0;
    }
  }
}

absl::StatusOr<SpriteFrame> Animator::GetCurrentFrame() const {
  if (!config_.has_value()) {
    return absl::FailedPreconditionError("No sprite config set.");
  }
  if (config_->sprite_frames.empty()) {
    return absl::FailedPreconditionError("Sprite config has no frames.");
  }
  if (current_frame_index_ < 0 || current_frame_index_ >= config_->sprite_frames.size()) {
    return absl::InternalError("Current frame index out of bounds.");
  }

  return config_->sprite_frames[current_frame_index_];
}

bool Animator::IsActive() const { return config_.has_value() && !config_->sprite_frames.empty(); }

}  // namespace zebes
