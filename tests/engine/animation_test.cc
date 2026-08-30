#include "engine/animation.h"

#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "objects/sprite.h"

namespace zebes {
namespace {

TEST(AnimationCursorTest, ReadsFramesThatChangeAfterPlaybackStarts) {
  AnimationCursor cursor;
  std::vector<SpriteFrame> frames = {
      {.index = 0, .texture_x = 10, .frames_per_cycle = 1},
  };

  ASSERT_TRUE(cursor.GetCurrentFrame(frames).ok());
  cursor.Update(frames, SpritePlaybackMode::kLoop);

  frames.push_back({.index = 1, .texture_x = 20, .frames_per_cycle = 1});
  cursor.Update(frames, SpritePlaybackMode::kLoop);

  ASSERT_TRUE(cursor.GetCurrentFrame(frames).ok());
  EXPECT_EQ(cursor.GetCurrentFrame(frames)->index, 1);
  EXPECT_EQ(cursor.GetCurrentFrame(frames)->texture_x, 20);
}

TEST(AnimationCursorTest, UsesEachFramesDuration) {
  AnimationCursor cursor;
  const std::vector<SpriteFrame> frames = {
      {.index = 0, .frames_per_cycle = 2},
      {.index = 1, .frames_per_cycle = 1},
  };

  cursor.Update(frames, SpritePlaybackMode::kLoop);
  ASSERT_TRUE(cursor.GetCurrentFrame(frames).ok());
  EXPECT_EQ(cursor.GetCurrentFrame(frames)->index, 0);

  cursor.Update(frames, SpritePlaybackMode::kLoop);
  EXPECT_EQ(cursor.GetCurrentFrame(frames)->index, 1);

  cursor.Update(frames, SpritePlaybackMode::kLoop);
  EXPECT_EQ(cursor.GetCurrentFrame(frames)->index, 0);
}

TEST(AnimationCursorTest, TreatsNonPositiveDurationAsOneTick) {
  AnimationCursor cursor;
  const std::vector<SpriteFrame> frames = {
      {.index = 0, .frames_per_cycle = 0},
      {.index = 1, .frames_per_cycle = -2},
  };

  cursor.Update(frames, SpritePlaybackMode::kLoop);
  ASSERT_TRUE(cursor.GetCurrentFrame(frames).ok());
  EXPECT_EQ(cursor.GetCurrentFrame(frames)->index, 1);

  cursor.Update(frames, SpritePlaybackMode::kLoop);
  EXPECT_EQ(cursor.GetCurrentFrame(frames)->index, 0);
}

TEST(AnimationCursorTest, HandlesFramesRemovedDuringPlayback) {
  AnimationCursor cursor;
  std::vector<SpriteFrame> frames = {
      {.index = 0, .frames_per_cycle = 1},
      {.index = 1, .frames_per_cycle = 1},
  };
  cursor.Update(frames, SpritePlaybackMode::kLoop);
  ASSERT_EQ(cursor.GetCurrentFrame(frames)->index, 1);

  frames.resize(1);

  ASSERT_TRUE(cursor.GetCurrentFrame(frames).ok());
  EXPECT_EQ(cursor.GetCurrentFrame(frames)->index, 0);
  cursor.Update(frames, SpritePlaybackMode::kLoop);
  EXPECT_EQ(cursor.GetCurrentFrame(frames)->index, 0);
}

TEST(AnimationCursorTest, EmptyFramesFailQueriesAndAreInactive) {
  AnimationCursor cursor;
  const std::vector<SpriteFrame> frames;

  EXPECT_FALSE(cursor.IsActive(frames));
  EXPECT_EQ(cursor.GetCurrentFrame(frames).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(cursor.GetCurrentFrameIndex(frames).status().code(),
            absl::StatusCode::kFailedPrecondition);
  cursor.Update(frames, SpritePlaybackMode::kLoop);
}

TEST(AnimationCursorTest, ResetRestartsAtFirstFrame) {
  AnimationCursor cursor;
  const std::vector<SpriteFrame> frames = {
      {.index = 0, .frames_per_cycle = 1},
      {.index = 1, .frames_per_cycle = 1},
  };

  cursor.Update(frames, SpritePlaybackMode::kLoop);
  ASSERT_EQ(cursor.GetCurrentFrame(frames)->index, 1);
  cursor.Reset();
  ASSERT_EQ(cursor.GetCurrentFrameIndex(frames).value(), 0);
  EXPECT_EQ(cursor.GetCurrentFrame(frames)->index, 0);
}

TEST(AnimationCursorTest, HoldLastPlaybackStopsOnTheFinalFrame) {
  AnimationCursor cursor;
  const std::vector<SpriteFrame> frames = {
      {.index = 0, .frames_per_cycle = 2},
      {.index = 1, .frames_per_cycle = 2},
  };

  cursor.Update(frames, SpritePlaybackMode::kHoldLast);
  EXPECT_EQ(cursor.GetCurrentFrameIndex(frames).value(), 0);
  cursor.Update(frames, SpritePlaybackMode::kHoldLast);
  EXPECT_EQ(cursor.GetCurrentFrameIndex(frames).value(), 1);

  for (int tick = 0; tick < 20; ++tick) {
    cursor.Update(frames, SpritePlaybackMode::kHoldLast);
  }
  EXPECT_EQ(cursor.GetCurrentFrameIndex(frames).value(), 1);

  cursor.Reset();
  EXPECT_EQ(cursor.GetCurrentFrameIndex(frames).value(), 0);
}

}  // namespace
}  // namespace zebes
