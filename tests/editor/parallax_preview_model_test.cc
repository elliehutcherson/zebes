#include "editor/parallax_theme_editor/parallax_preview_model.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

TEST(ParallaxPreviewModelTest, FindsStableLevelZoneContextsForATheme) {
  const std::vector<Level> levels = {
      {.id = "first",
       .name = "First",
       .width = 2000,
       .height = 1000,
       .zones = {{.id = 3,
                  .name = "Cave",
                  .theme_id = "cave",
                  .min_point = {0, 0},
                  .max_point = {960, 540}}}},
      {.id = "second",
       .name = "Second",
       .width = 3000,
       .height = 1200,
       .zones = {{.id = 7,
                  .name = "Other",
                  .theme_id = "other",
                  .min_point = {0, 0},
                  .max_point = {100, 100}}}},
  };

  const std::vector<ParallaxThemeUsage> usages = FindParallaxThemeUsages(levels, "cave");

  ASSERT_EQ(usages.size(), 1);
  EXPECT_EQ(usages[0].level_id, "first");
  EXPECT_EQ(usages[0].zone_id, 3);
  EXPECT_EQ(usages[0].route.min, Vec(0, 0));
  EXPECT_EQ(usages[0].route.max, Vec(960, 540));
  EXPECT_EQ(usages[0].world.max, Vec(2000, 1000));
}

TEST(ParallaxPreviewModelTest, ContextRouteUsesReachableCameraCentersNotWorldCorners) {
  ASSERT_OK_AND_ASSIGN(
      const CameraCenterRoute route,
      ResolveCameraCenterRoute({.min = {0, 0}, .max = {960, 540}}, {960, 540}, 1.0,
                               CameraWorldBounds{.min = {0, 0}, .max = {960, 540}}));

  EXPECT_EQ(route.min, Vec(480, 270));
  EXPECT_EQ(route.max, Vec(480, 270));
}

TEST(ParallaxPreviewModelTest, ManualRouteIsAlreadyCameraCenterSpace) {
  ASSERT_OK_AND_ASSIGN(
      const CameraCenterRoute route,
      ResolveCameraCenterRoute({.min = {10, 20}, .max = {30, 40}}, {960, 540}, 0.5));
  EXPECT_EQ(route.min, Vec(10, 20));
  EXPECT_EQ(route.max, Vec(30, 40));
}

TEST(ParallaxPreviewModelTest, RejectsAContextTheCameraCannotReachAtTheZoom) {
  EXPECT_EQ(ResolveCameraCenterRoute({.min = {0, 0}, .max = {100, 100}}, {960, 540}, 1.0,
                                     CameraWorldBounds{.min = {1000, 1000}, .max = {3000, 2000}})
                .status()
                .code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(ParallaxPreviewModelTest, InterpolationClampsProgressToTheRoute) {
  const CameraCenterRoute route{.min = {100, 200}, .max = {300, 600}};
  EXPECT_EQ(InterpolateCameraCenter(route, -1.0, 0.25), Vec(100, 300));
  EXPECT_EQ(InterpolateCameraCenter(route, 0.5, 2.0), Vec(200, 600));
}

}  // namespace
}  // namespace zebes
