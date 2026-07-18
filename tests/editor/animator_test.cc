#include "editor/animator.h"

#include <vector>

#include "gtest/gtest.h"
#include "objects/sprite.h"

namespace zebes {
namespace {

TEST(AnimatorTest, ReadsLiveFramesAddedAfterPlaybackStarts) {
  Animator animator;
  std::vector<SpriteFrame> frames = {
      {.index = 0, .texture_x = 10, .frames_per_cycle = 1},
  };

  ASSERT_TRUE(animator.GetCurrentFrame(frames).ok());
  animator.Update(frames);

  frames.push_back({.index = 1, .texture_x = 20, .frames_per_cycle = 1});
  animator.Update(frames);

  absl::StatusOr<SpriteFrame> current = animator.GetCurrentFrame(frames);
  ASSERT_TRUE(current.ok());
  EXPECT_EQ(current->index, 1);
  EXPECT_EQ(current->texture_x, 20);
}

TEST(AnimatorTest, UsesEachFramesDuration) {
  Animator animator;
  std::vector<SpriteFrame> frames = {
      {.index = 0, .frames_per_cycle = 2},
      {.index = 1, .frames_per_cycle = 1},
  };

  animator.Update(frames);
  ASSERT_TRUE(animator.GetCurrentFrame(frames).ok());
  EXPECT_EQ(animator.GetCurrentFrame(frames)->index, 0);

  animator.Update(frames);
  EXPECT_EQ(animator.GetCurrentFrame(frames)->index, 1);

  animator.Update(frames);
  EXPECT_EQ(animator.GetCurrentFrame(frames)->index, 0);
}

TEST(AnimatorTest, HandlesFramesRemovedDuringPlayback) {
  Animator animator;
  std::vector<SpriteFrame> frames = {
      {.index = 0, .frames_per_cycle = 1},
      {.index = 1, .frames_per_cycle = 1},
  };
  animator.Update(frames);
  ASSERT_EQ(animator.GetCurrentFrame(frames)->index, 1);

  frames.resize(1);

  ASSERT_TRUE(animator.GetCurrentFrame(frames).ok());
  EXPECT_EQ(animator.GetCurrentFrame(frames)->index, 0);
  animator.Update(frames);
  EXPECT_EQ(animator.GetCurrentFrame(frames)->index, 0);
}

TEST(AnimatorTest, EmptyFramesAreInactive) {
  Animator animator;
  const std::vector<SpriteFrame> frames;

  EXPECT_FALSE(animator.IsActive(frames));
  EXPECT_FALSE(animator.GetCurrentFrame(frames).ok());
  animator.Update(frames);
}

}  // namespace
}  // namespace zebes
