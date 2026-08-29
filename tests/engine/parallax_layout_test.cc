#include "engine/parallax_layout.h"

#include <gtest/gtest.h>

#include <vector>

#include "macros.h"

namespace zebes {
namespace {

ParallaxLayer SingleElementLayer(Vec scroll_factor = {0, 0}) {
  return {
      .name = "Layer",
      .scroll_factor = scroll_factor,
      .elements = {{.id = 7, .name = "Element", .texture_id = "texture"}},
  };
}

const std::vector<ParallaxElementSize> kSingleElementSize = {
    {.element_id = 7, .width = 100, .height = 50}};

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
  ParallaxLayer layer = SingleElementLayer();

  ASSERT_OK_AND_ASSIGN(const ParallaxLayout layout,
                       CalculateParallaxLayout(camera, layer, kSingleElementSize));

  EXPECT_DOUBLE_EQ(layout.origin.x, 300);
  EXPECT_DOUBLE_EQ(layout.origin.y, 250);
}

TEST(ParallaxLayoutTest, FullyScrollingLayerRemainsAnchoredInWorld) {
  Camera camera{
      .position = {500, 400},
      .zoom = 2,
      .viewport_width = 800,
      .viewport_height = 600,
  };
  ParallaxLayer layer = SingleElementLayer({1, 1});
  layer.offset = {25, 50};

  ASSERT_OK_AND_ASSIGN(const ParallaxLayout layout,
                       CalculateParallaxLayout(camera, layer, kSingleElementSize));

  EXPECT_DOUBLE_EQ(layout.origin.x, 25);
  EXPECT_DOUBLE_EQ(layout.origin.y, 50);
}

TEST(ParallaxLayoutTest, RepeatedLayerCoversVisibleWorld) {
  Camera camera{
      .position = {500, 400},
      .zoom = 2,
      .viewport_width = 800,
      .viewport_height = 600,
  };
  ParallaxLayer layer = SingleElementLayer();
  layer.elements[0].scale = 2.0f;
  layer.repeat_period = {200, 100};

  ASSERT_OK_AND_ASSIGN(const ParallaxLayout layout,
                       CalculateParallaxLayout(camera, layer, kSingleElementSize));

  ASSERT_EQ(layout.elements.size(), 6);
  EXPECT_EQ(layout.elements.front().repeat_column, 0);
  EXPECT_EQ(layout.elements.front().repeat_row, 0);
  EXPECT_DOUBLE_EQ(layout.elements.front().bounds.min.x, 300);
  EXPECT_DOUBLE_EQ(layout.elements.front().bounds.max.x, 500);
  EXPECT_EQ(layout.elements.back().repeat_column, 1);
  EXPECT_EQ(layout.elements.back().repeat_row, 2);
}

TEST(ParallaxLayoutTest, NonRepeatingLayerDrawsOnlyItsBaseTile) {
  Camera camera{
      .position = {500, 400},
      .zoom = 1,
      .viewport_width = 800,
      .viewport_height = 600,
  };
  ParallaxLayer layer = SingleElementLayer();

  ASSERT_OK_AND_ASSIGN(const ParallaxLayout layout,
                       CalculateParallaxLayout(camera, layer, kSingleElementSize));

  ASSERT_EQ(layout.elements.size(), 1);
  EXPECT_EQ(layout.elements[0].repeat_column, 0);
  EXPECT_EQ(layout.elements[0].repeat_row, 0);
}

TEST(ParallaxLayoutTest, FiniteOffscreenAxisSkipsAllRepeatCells) {
  Camera camera{
      .position = {500, 400},
      .zoom = 1,
      .viewport_width = 800,
      .viewport_height = 600,
  };
  ParallaxLayer layer = SingleElementLayer({1, 1});
  layer.offset = {100, -1000};
  layer.repeat_period.x = 0.01;

  ASSERT_OK_AND_ASSIGN(const ParallaxLayout layout,
                       CalculateParallaxLayout(camera, layer, kSingleElementSize));

  EXPECT_TRUE(layout.elements.empty());
}

TEST(ParallaxLayoutTest, RejectsInvalidGeometry) {
  Camera camera{
      .position = {500, 400},
      .zoom = 1,
      .viewport_width = 800,
      .viewport_height = 600,
  };
  ParallaxLayer layer = SingleElementLayer();

  EXPECT_FALSE(CalculateParallaxLayout(camera, layer, {}).ok());
  layer.elements[0].scale = 0;
  EXPECT_FALSE(CalculateParallaxLayout(camera, layer, kSingleElementSize).ok());
}

TEST(ParallaxLayoutTest, RefusesPathologicalRepeatBeforeBuildingInstances) {
  Camera camera{
      .position = {500, 400},
      .zoom = 1,
      .viewport_width = 800,
      .viewport_height = 600,
  };
  ParallaxLayer layer = SingleElementLayer();
  layer.repeat_period = {0.01, 0};

  EXPECT_EQ(CalculateParallaxLayout(camera, layer, kSingleElementSize).status().code(),
            absl::StatusCode::kResourceExhausted);
}

TEST(ParallaxLayoutTest, RepeatsCompleteCompositionInElementOrder) {
  Camera camera{.position = {50, 25}, .zoom = 1, .viewport_width = 100, .viewport_height = 50};
  ParallaxLayer layer{
      .name = "Near formations",
      .repeat_period = {100, 0},
      .elements =
          {
              {.id = 1, .name = "Left", .texture_id = "left", .position = {0, 0}},
              {.id = 2, .name = "Right", .texture_id = "right", .position = {60, 5}},
          },
  };
  const std::vector<ParallaxElementSize> sizes = {
      {.element_id = 1, .width = 40, .height = 40},
      {.element_id = 2, .width = 30, .height = 30},
  };

  ASSERT_OK_AND_ASSIGN(const ParallaxLayout layout, CalculateParallaxLayout(camera, layer, sizes));

  ASSERT_EQ(layout.elements.size(), 2);
  EXPECT_EQ(layout.elements[0].element_id, 1);
  EXPECT_EQ(layout.elements[1].element_id, 2);
  EXPECT_DOUBLE_EQ(layout.elements[1].bounds.min.x, 60);
}

TEST(ParallaxLayoutTest, CompositionBoundsIncludePositionAndScale) {
  ParallaxLayer layer{
      .name = "Near formations",
      .elements =
          {
              {.id = 1, .name = "A", .texture_id = "a", .position = {-20, 10}, .scale = 2},
              {.id = 2, .name = "B", .texture_id = "b", .position = {100, -5}},
          },
  };

  ASSERT_OK_AND_ASSIGN(
      const WorldRect bounds,
      CalculateParallaxCompositionBounds(layer, {{.element_id = 1, .width = 30, .height = 20},
                                                 {.element_id = 2, .width = 25, .height = 10}}));

  EXPECT_DOUBLE_EQ(bounds.min.x, -20);
  EXPECT_DOUBLE_EQ(bounds.min.y, -5);
  EXPECT_DOUBLE_EQ(bounds.max.x, 125);
  EXPECT_DOUBLE_EQ(bounds.max.y, 50);
}

TEST(ParallaxLayoutTest, CameraFrameCentersAndFitsBoundsWithPadding) {
  std::optional<CameraFrame> frame =
      CalculateCameraFrame({.min = {100, 200}, .max = {500, 400}}, 1000, 800, 0.1);

  ASSERT_TRUE(frame.has_value());
  EXPECT_DOUBLE_EQ(frame->position.x, 300);
  EXPECT_DOUBLE_EQ(frame->position.y, 300);
  EXPECT_DOUBLE_EQ(frame->zoom, 2);
}

TEST(ParallaxLayoutTest, CameraFrameRejectsInvalidBoundsAndViewport) {
  EXPECT_FALSE(CalculateCameraFrame({.min = {100, 0}, .max = {100, 200}}, 800, 600).has_value());
  EXPECT_FALSE(CalculateCameraFrame({.min = {0, 0}, .max = {100, 200}}, 0, 600).has_value());
}

TEST(ParallaxLayoutTest, ConstrainedFrameKeepsLongHorizontalZoneCenterInsideWorld) {
  const VisibleWorldBounds zone{.min = {0, 1024}, .max = {8192, 2048}};
  const VisibleWorldBounds world{.min = {0, 0}, .max = {1048576, 4096}};

  std::optional<CameraFrame> frame = CalculateConstrainedCameraFrame(zone, world, 1031, 926);

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
}

TEST(ParallaxLayoutTest, ConstrainedFrameKeepsTallVerticalZoneCenterInsideWorld) {
  const VisibleWorldBounds zone{.min = {1024, 0}, .max = {2048, 65536}};
  const VisibleWorldBounds world{.min = {0, 0}, .max = {4096, 1048576}};

  std::optional<CameraFrame> frame = CalculateConstrainedCameraFrame(zone, world, 926, 1031);

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
}

TEST(ParallaxLayoutTest, ConstrainedFrameRejectsInvalidWorldBounds) {
  EXPECT_FALSE(CalculateConstrainedCameraFrame({.min = {0, 0}, .max = {100, 100}},
                                               {.min = {0, 0}, .max = {0, 100}}, 800, 600)
                   .has_value());
}

}  // namespace
}  // namespace zebes
