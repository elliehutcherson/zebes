#pragma once

#include <vector>

#include "absl/status/statusor.h"
#include "objects/sprite.h"

namespace zebes {

// Playback cursor for a sprite animation: a frame index and a tick count, and
// nothing else. Frames are passed in on every call rather than stored, so the
// editor can play an animation the user is still editing.
//
// That means the list can shrink under a running animator. Update() rewinds
// when the index no longer exists and the queries wrap, so a stale index costs
// a visible jump rather than a crash.
//
// One Update() is one tick, and a frame lasts frames_per_cycle ticks. The
// caller sets the rate by choosing how often to call.
class Animator {
 public:
  Animator() = default;
  ~Animator() = default;

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
