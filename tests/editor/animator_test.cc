#include "editor/animator.h"

#include <vector>

#include "gtest/gtest.h"
#include "macros.h"
#include "objects/sprite.h"

namespace zebes {
namespace {

TEST(AnimatorTest, ReadsLiveFramesAddedAfterPlaybackStarts) {
  Animator animator;
  std::vector<SpriteFrame> frames = {
      {.index = 0, .texture_x = 10, .frames_per_cycle = 1},
  };

  ASSERT_OK(animator.GetCurrentFrame(frames));
  animator.Update(frames, SpritePlaybackMode::kLoop);

  frames.push_back({.index = 1, .texture_x = 20, .frames_per_cycle = 1});
  animator.Update(frames, SpritePlaybackMode::kLoop);

  absl::StatusOr<SpriteFrame> current = animator.GetCurrentFrame(frames);
  ASSERT_OK(current);
  EXPECT_EQ(current->index, 1);
  EXPECT_EQ(current->texture_x, 20);
}

TEST(AnimatorTest, UsesEachFramesDuration) {
  Animator animator;
  std::vector<SpriteFrame> frames = {
      {.index = 0, .frames_per_cycle = 2},
      {.index = 1, .frames_per_cycle = 1},
  };

  animator.Update(frames, SpritePlaybackMode::kLoop);
  ASSERT_OK(animator.GetCurrentFrame(frames));
  EXPECT_EQ(animator.GetCurrentFrame(frames)->index, 0);

  animator.Update(frames, SpritePlaybackMode::kLoop);
  EXPECT_EQ(animator.GetCurrentFrame(frames)->index, 1);

  animator.Update(frames, SpritePlaybackMode::kLoop);
  EXPECT_EQ(animator.GetCurrentFrame(frames)->index, 0);
}

TEST(AnimatorTest, HandlesFramesRemovedDuringPlayback) {
  Animator animator;
  std::vector<SpriteFrame> frames = {
      {.index = 0, .frames_per_cycle = 1},
      {.index = 1, .frames_per_cycle = 1},
  };
  animator.Update(frames, SpritePlaybackMode::kLoop);
  ASSERT_EQ(animator.GetCurrentFrame(frames)->index, 1);

  frames.resize(1);

  ASSERT_OK(animator.GetCurrentFrame(frames));
  EXPECT_EQ(animator.GetCurrentFrame(frames)->index, 0);
  animator.Update(frames, SpritePlaybackMode::kLoop);
  EXPECT_EQ(animator.GetCurrentFrame(frames)->index, 0);
}

TEST(AnimatorTest, EmptyFramesAreInactive) {
  Animator animator;
  const std::vector<SpriteFrame> frames;

  EXPECT_FALSE(animator.IsActive(frames));
  EXPECT_FALSE(animator.GetCurrentFrame(frames).ok());
  animator.Update(frames, SpritePlaybackMode::kLoop);
}

}  // namespace
}  // namespace zebes
