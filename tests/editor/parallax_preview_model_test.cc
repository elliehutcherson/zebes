#include "editor/parallax_theme_editor/parallax_preview_model.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

TEST(ParallaxPreviewModelTest, CapacityErrorPausesOnlyThePreview) {
  EXPECT_TRUE(IsRecoverableParallaxPreviewError(
      absl::ResourceExhaustedError("too many preview instances")));
  EXPECT_FALSE(IsRecoverableParallaxPreviewError(absl::InternalError("renderer failed")));
}

TEST(ParallaxPreviewModelTest, ManualFallbackExpandsAStationaryCameraRoute) {
  const CameraCenterRoute route =
      EnsureNavigableManualCameraRoute({.min = {480, 270}, .max = {480, 270}}, {960, 540});

  EXPECT_EQ(route.min, Vec(480, 270));
  EXPECT_EQ(route.max, Vec(1440, 810));
}

TEST(ParallaxPreviewModelTest, ManualFallbackPreservesAnAuthoredRoute) {
  const CameraCenterRoute route =
      EnsureNavigableManualCameraRoute({.min = {100, 200}, .max = {500, 600}}, {960, 540});

  EXPECT_EQ(route.min, Vec(100, 200));
  EXPECT_EQ(route.max, Vec(500, 600));
}

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

TEST(ParallaxPreviewModelTest, CameraTravelTracksCanvasMovementAndClampsToRoute) {
  const CameraCenterRoute route{.min = {100, 200}, .max = {300, 600}};

  ASSERT_OK_AND_ASSIGN(const Vec travel, CalculateCameraTravel(route, {250, 1000}));

  EXPECT_EQ(travel, Vec(0.75, 1.0));
  EXPECT_EQ(InterpolateCameraCenter(route, travel.x, travel.y), Vec(250, 600));
}

TEST(ParallaxPreviewModelTest, StationaryCameraRouteAxisHasZeroTravel) {
  ASSERT_OK_AND_ASSIGN(const Vec travel,
                       CalculateCameraTravel({.min = {480, 200}, .max = {480, 600}}, {900, 300}));

  EXPECT_EQ(travel, Vec(0.0, 0.25));
}

TEST(ParallaxPreviewModelTest, IncompleteElementsDoNotHideValidDraftArtwork) {
  ParallaxTheme draft{
      .id = "cave",
      .name = "Cave",
      .layers =
          {
              {.name = "Far", .elements = {{.id = 0, .name = "Fill", .texture_id = "fill"}}},
              {.name = "Near",
               .elements =
                   {
                       {.id = 0, .name = "Formation", .texture_id = "formation"},
                       {.id = 1, .name = "New Element"},
                   }},
              {.name = "New Layer", .elements = {{.id = 0, .name = "New Element"}}},
          },
  };

  const ParallaxPreviewTheme preview = BuildParallaxPreviewTheme(draft);

  ASSERT_EQ(preview.theme.layers.size(), 2);
  EXPECT_EQ(preview.theme.layers[0].elements.size(), 1);
  EXPECT_EQ(preview.theme.layers[1].elements.size(), 1);
  EXPECT_EQ(preview.source_layer_indices, (std::vector<int>{0, 1}));
  EXPECT_EQ(preview.omitted_elements, 2);
  EXPECT_EQ(FindPreviewLayerIndex(preview, 1), 1);
  EXPECT_EQ(FindPreviewLayerIndex(preview, 2), std::nullopt);
  EXPECT_EQ(draft.layers[1].elements.size(), 2);
}

}  // namespace
}  // namespace zebes
