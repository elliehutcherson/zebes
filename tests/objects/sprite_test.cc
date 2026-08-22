#include "objects/sprite.h"

#include <limits>

#include "gtest/gtest.h"

namespace zebes {
namespace {

TEST(SpriteFrameRenderBoundsTest, PlacesRenderRectangleRelativeToSpriteOrigin) {
  const SpriteFrame frame{
      .render_w = 32,
      .render_h = 64,
      .offset_x = -16,
      .offset_y = -56,
  };

  const SpriteFrameRenderBounds bounds = CalculateSpriteFrameRenderBounds(frame);

  EXPECT_EQ(bounds.left, -16);
  EXPECT_EQ(bounds.top, -56);
  EXPECT_EQ(bounds.right, 16);
  EXPECT_EQ(bounds.bottom, 8);
  EXPECT_TRUE(bounds.IsValid());
}

TEST(SpriteFrameRenderBoundsTest, RejectsNonPositiveRenderArea) {
  EXPECT_FALSE(
      CalculateSpriteFrameRenderBounds(SpriteFrame{.render_w = 0, .render_h = 1}).IsValid());
  EXPECT_FALSE(
      CalculateSpriteFrameRenderBounds(SpriteFrame{.render_w = 1, .render_h = -1}).IsValid());
}

TEST(SpriteFrameRenderBoundsTest, WidensArithmeticBeforeAddingDimensions) {
  const SpriteFrame frame{
      .render_w = std::numeric_limits<int>::max(),
      .render_h = 1,
      .offset_x = std::numeric_limits<int>::max(),
  };

  const SpriteFrameRenderBounds bounds = CalculateSpriteFrameRenderBounds(frame);

  EXPECT_EQ(bounds.right, 2LL * std::numeric_limits<int>::max());
  EXPECT_TRUE(bounds.IsValid());
}

}  // namespace
}  // namespace zebes
