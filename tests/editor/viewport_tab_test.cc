#include "editor/level_editor/viewport_tab.h"

#include "gtest/gtest.h"

namespace zebes {
namespace {

TEST(CalculateParallaxOffsetTest, ZeroCameraPos) {
  EXPECT_DOUBLE_EQ(CalculateParallaxOffset(0.0, 0.5, 2.0), 0.0);
}

TEST(CalculateParallaxOffsetTest, FullScrollFactor) {
  // scroll_factor=1.0 means layer moves fully with camera
  EXPECT_DOUBLE_EQ(CalculateParallaxOffset(100.0, 1.0, 1.0), 100.0);
}

TEST(CalculateParallaxOffsetTest, ZeroScrollFactor) {
  // scroll_factor=0.0 means layer is fixed (distant background)
  EXPECT_DOUBLE_EQ(CalculateParallaxOffset(100.0, 0.0, 1.0), 0.0);
}

TEST(CalculateParallaxOffsetTest, HalfScrollFactor) {
  EXPECT_DOUBLE_EQ(CalculateParallaxOffset(200.0, 0.5, 1.0), 100.0);
}

TEST(CalculateParallaxOffsetTest, ZoomScales) {
  EXPECT_DOUBLE_EQ(CalculateParallaxOffset(100.0, 1.0, 2.0), 200.0);
}

}  // namespace
}  // namespace zebes
