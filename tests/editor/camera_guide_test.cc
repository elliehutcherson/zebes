#include "editor/level_editor/camera_guide.h"

#include "gtest/gtest.h"

namespace zebes {
namespace {

TEST(CameraGuideTest, CentersConfiguredGameViewOnCamera) {
  std::optional<CameraGuide> guide =
      CalculateCameraGuide({100, 200}, GameViewSize{.width = 640, .height = 360});

  ASSERT_TRUE(guide.has_value());
  EXPECT_DOUBLE_EQ(guide->center.x, 100);
  EXPECT_DOUBLE_EQ(guide->center.y, 200);
  EXPECT_DOUBLE_EQ(guide->min.x, -220);
  EXPECT_DOUBLE_EQ(guide->min.y, 20);
  EXPECT_DOUBLE_EQ(guide->max.x, 420);
  EXPECT_DOUBLE_EQ(guide->max.y, 380);
}

TEST(CameraGuideTest, PreservesFractionalEdgesForOddDimensions) {
  std::optional<CameraGuide> guide =
      CalculateCameraGuide({0, 0}, GameViewSize{.width = 641, .height = 361});

  ASSERT_TRUE(guide.has_value());
  EXPECT_DOUBLE_EQ(guide->min.x, -320.5);
  EXPECT_DOUBLE_EQ(guide->min.y, -180.5);
  EXPECT_DOUBLE_EQ(guide->max.x, 320.5);
  EXPECT_DOUBLE_EQ(guide->max.y, 180.5);
}

TEST(CameraGuideTest, RejectsNonPositiveDimensions) {
  EXPECT_FALSE(
      CalculateCameraGuide({0, 0}, GameViewSize{.width = 0, .height = 360}).has_value());
  EXPECT_FALSE(
      CalculateCameraGuide({0, 0}, GameViewSize{.width = 640, .height = -1}).has_value());
}

}  // namespace
}  // namespace zebes
