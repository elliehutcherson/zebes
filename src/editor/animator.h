#pragma once

#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "objects/sprite_interface.h"

namespace zebes {

class Animator {
 public:
  Animator() = default;
  ~Animator() = default;

  // Set the sprite config and reset animation state.
  void SetSpriteConfig(const SpriteConfig& config);

  // Advance the animation by one tick.
  void Update();

  // Get the current frame of the animation.
  // Returns error if no sprite config is set or frames are empty.
  absl::StatusOr<SpriteFrame> GetCurrentFrame() const;

  // Check if an animation is currently active.
  bool IsActive() const;

 private:
  std::optional<SpriteConfig> config_;
  int current_frame_index_ = 0;
  int tick_counter_ = 0;
};

}  // namespace zebes
