#include "editor/level_editor/parallax_layout.h"

#include <gtest/gtest.h>

namespace zebes {
namespace {

TEST(ParallaxLayoutTest, VisibleBoundsAccountForZoom) {
  Camera camera{
      .position = {500, 400},
      .zoom = 2,
      .viewport_width = 800,
      .viewport_height = 600,
  };

  VisibleWorldBounds bounds = CalculateVisibleWorldBounds(camera);

  EXPECT_DOUBLE_EQ(bounds.min.x, 300);
  EXPECT_DOUBLE_EQ(bounds.min.y, 250);
  EXPECT_DOUBLE_EQ(bounds.max.x, 700);
  EXPECT_DOUBLE_EQ(bounds.max.y, 550);
}

TEST(ParallaxLayoutTest, FixedLayerStartsAtVisibleWorldOriginAtAnyZoom) {
  Camera camera{
      .position = {500, 400},
      .zoom = 2,
      .viewport_width = 800,
      .viewport_height = 600,
  };
  ParallaxLayer layer{
      .scroll_factor = {0, 0},
  };

  std::optional<ParallaxLayout> layout = CalculateParallaxLayout(camera, layer, 100, 50);

  ASSERT_TRUE(layout.has_value());
  EXPECT_DOUBLE_EQ(layout->origin.x, 300);
  EXPECT_DOUBLE_EQ(layout->origin.y, 250);
}

TEST(ParallaxLayoutTest, FullyScrollingLayerRemainsAnchoredInWorld) {
  Camera camera{
      .position = {500, 400},
      .zoom = 2,
      .viewport_width = 800,
      .viewport_height = 600,
  };
  ParallaxLayer layer{
      .scroll_factor = {1, 1},
      .offset = {25, 50},
  };

  std::optional<ParallaxLayout> layout = CalculateParallaxLayout(camera, layer, 100, 50);

  ASSERT_TRUE(layout.has_value());
  EXPECT_DOUBLE_EQ(layout->origin.x, 25);
  EXPECT_DOUBLE_EQ(layout->origin.y, 50);
}

TEST(ParallaxLayoutTest, RepeatedLayerCoversVisibleWorld) {
  Camera camera{
      .position = {500, 400},
      .zoom = 2,
      .viewport_width = 800,
      .viewport_height = 600,
  };
  ParallaxLayer layer{
      .scroll_factor = {0, 0},
      .base_scale = 2,
      .repeat_x = true,
      .repeat_y = true,
  };

  std::optional<ParallaxLayout> layout = CalculateParallaxLayout(camera, layer, 100, 50);

  ASSERT_TRUE(layout.has_value());
  EXPECT_DOUBLE_EQ(layout->tile_width, 200);
  EXPECT_DOUBLE_EQ(layout->tile_height, 100);
  EXPECT_EQ(layout->first_column, 0);
  EXPECT_EQ(layout->last_column, 2);
  EXPECT_EQ(layout->first_row, 0);
  EXPECT_EQ(layout->last_row, 3);
}

TEST(ParallaxLayoutTest, NonRepeatingLayerDrawsOnlyItsBaseTile) {
  Camera camera{
      .position = {500, 400},
      .zoom = 1,
      .viewport_width = 800,
      .viewport_height = 600,
  };
  ParallaxLayer layer{
      .scroll_factor = {0.5, 0.5},
  };

  std::optional<ParallaxLayout> layout = CalculateParallaxLayout(camera, layer, 100, 50);

  ASSERT_TRUE(layout.has_value());
  EXPECT_EQ(layout->first_column, 0);
  EXPECT_EQ(layout->last_column, 0);
  EXPECT_EQ(layout->first_row, 0);
  EXPECT_EQ(layout->last_row, 0);
}

TEST(ParallaxLayoutTest, RejectsInvalidGeometry) {
  Camera camera{
      .position = {500, 400},
      .zoom = 1,
      .viewport_width = 800,
      .viewport_height = 600,
  };
  ParallaxLayer layer;

  EXPECT_FALSE(CalculateParallaxLayout(camera, layer, 0, 50).has_value());
  layer.base_scale = 0;
  EXPECT_FALSE(CalculateParallaxLayout(camera, layer, 100, 50).has_value());
}

TEST(ParallaxLayoutTest, ActiveZoneUsesReferencePointInsteadOfViewportOverlap) {
  std::vector<ParallaxZone> zones{{
      .id = 7,
      .theme_id = 3,
      .min_point = {0, 1024},
      .max_point = {8192, 2048},
  }};

  EXPECT_FALSE(ResolveActiveParallaxZone(zones, {400, 300}).has_value());

  std::optional<ActiveParallaxZone> active = ResolveActiveParallaxZone(zones, {400, 1200});
  ASSERT_TRUE(active.has_value());
  EXPECT_EQ(active->zone_id, 7);
  EXPECT_EQ(active->theme_id, 3);
}

TEST(ParallaxLayoutTest, ActiveZoneUsesHalfOpenBounds) {
  std::vector<ParallaxZone> zones{
      {.id = 1, .theme_id = 10, .min_point = {0, 0}, .max_point = {100, 100}},
      {.id = 2, .theme_id = 20, .min_point = {100, 0}, .max_point = {200, 100}},
  };

  std::optional<ActiveParallaxZone> active = ResolveActiveParallaxZone(zones, {100, 50});

  ASSERT_TRUE(active.has_value());
  EXPECT_EQ(active->zone_id, 2);
}

TEST(ParallaxLayoutTest, LaterOverlappingZoneHasPriority) {
  std::vector<ParallaxZone> zones{
      {.id = 1, .theme_id = 10, .min_point = {0, 0}, .max_point = {200, 200}},
      {.id = 2, .theme_id = 20, .min_point = {50, 50}, .max_point = {150, 150}},
  };

  std::optional<ActiveParallaxZone> active = ResolveActiveParallaxZone(zones, {100, 100});

  ASSERT_TRUE(active.has_value());
  EXPECT_EQ(active->zone_id, 2);
  EXPECT_EQ(active->theme_id, 20);
}

TEST(ParallaxLayoutTest, CameraFrameCentersAndFitsBoundsWithPadding) {
  std::optional<CameraFrame> frame = CalculateCameraFrame(
      {.min = {100, 200}, .max = {500, 400}}, 1000, 800, 0.1);

  ASSERT_TRUE(frame.has_value());
  EXPECT_DOUBLE_EQ(frame->position.x, 300);
  EXPECT_DOUBLE_EQ(frame->position.y, 300);
  EXPECT_DOUBLE_EQ(frame->zoom, 2);
}

TEST(ParallaxLayoutTest, CameraFrameRejectsInvalidBoundsAndViewport) {
  EXPECT_FALSE(
      CalculateCameraFrame({.min = {100, 0}, .max = {100, 200}}, 800, 600).has_value());
  EXPECT_FALSE(CalculateCameraFrame({.min = {0, 0}, .max = {100, 200}}, 0, 600).has_value());
}

TEST(ParallaxLayoutTest, ConstrainedFrameKeepsLongHorizontalZoneCenterInsideWorld) {
  const VisibleWorldBounds zone{.min = {0, 1024}, .max = {8192, 2048}};
  const VisibleWorldBounds world{.min = {0, 0}, .max = {1048576, 4096}};

  std::optional<CameraFrame> frame =
      CalculateConstrainedCameraFrame(zone, world, 1031, 926);

  ASSERT_TRUE(frame.has_value());
  EXPECT_DOUBLE_EQ(frame->position.x, 4096);
  EXPECT_DOUBLE_EQ(frame->position.y, 1536);
  EXPECT_NEAR(frame->zoom, 926.0 / 3072.0, 1e-12);

  Camera camera{.position = frame->position,
                .zoom = frame->zoom,
                .viewport_width = 1031,
                .viewport_height = 926};
  VisibleWorldBounds visible = CalculateVisibleWorldBounds(camera);
  EXPECT_GE(visible.min.x, world.min.x);
  EXPECT_GE(visible.min.y, world.min.y);
  EXPECT_LE(visible.max.x, world.max.x);
  EXPECT_LE(visible.max.y, world.max.y);

  ParallaxZone active_zone{.id = 1, .min_point = zone.min, .max_point = zone.max};
  ASSERT_TRUE(ResolveActiveParallaxZone({active_zone}, frame->position).has_value());
}

TEST(ParallaxLayoutTest, ConstrainedFrameKeepsTallVerticalZoneCenterInsideWorld) {
  const VisibleWorldBounds zone{.min = {1024, 0}, .max = {2048, 65536}};
  const VisibleWorldBounds world{.min = {0, 0}, .max = {4096, 1048576}};

  std::optional<CameraFrame> frame =
      CalculateConstrainedCameraFrame(zone, world, 926, 1031);

  ASSERT_TRUE(frame.has_value());
  EXPECT_DOUBLE_EQ(frame->position.x, 1536);
  EXPECT_DOUBLE_EQ(frame->position.y, 32768);
  EXPECT_NEAR(frame->zoom, 926.0 / 3072.0, 1e-12);

  Camera camera{.position = frame->position,
                .zoom = frame->zoom,
                .viewport_width = 926,
                .viewport_height = 1031};
  VisibleWorldBounds visible = CalculateVisibleWorldBounds(camera);
  EXPECT_GE(visible.min.x, world.min.x);
  EXPECT_GE(visible.min.y, world.min.y);
  EXPECT_LE(visible.max.x, world.max.x);
  EXPECT_LE(visible.max.y, world.max.y);

  ParallaxZone active_zone{.id = 2, .min_point = zone.min, .max_point = zone.max};
  ASSERT_TRUE(ResolveActiveParallaxZone({active_zone}, frame->position).has_value());
}

TEST(ParallaxLayoutTest, ConstrainedFrameRejectsInvalidWorldBounds) {
  EXPECT_FALSE(CalculateConstrainedCameraFrame(
                   {.min = {0, 0}, .max = {100, 100}},
                   {.min = {0, 0}, .max = {0, 100}}, 800, 600)
                   .has_value());
}

}  // namespace
}  // namespace zebes
