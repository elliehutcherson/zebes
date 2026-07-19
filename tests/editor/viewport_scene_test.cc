#include "editor/level_editor/viewport_scene.h"

#include <map>
#include <vector>

#include "gtest/gtest.h"
#include "objects/entity.h"
#include "objects/sprite.h"

namespace zebes {
namespace {

TEST(ViewportSceneEntityTest, ComposesStableSpriteGeometryAndPresentationState) {
  int texture_owner = 0;
  Sprite sprite{
      .frames = {SpriteFrame{
          .texture_x = 8,
          .texture_y = 12,
          .texture_w = 16,
          .texture_h = 24,
          .render_w = 32,
          .render_h = 48,
          .offset_x = -10,
          .offset_y = -20,
      }},
      .texture_handle = TextureHandleAccess::Create(1, &texture_owner),
  };
  std::map<uint64_t, Entity> entities{
      {7, Entity{.id = 7, .transform = {.position = {100, 200}}, .sprite = &sprite}},
  };

  absl::StatusOr<std::vector<EntityRenderItem>> items = ComposeEntityRenderItems(
      entities, {.selected_entity_id = 7, .show_borders = true, .overlay_opacity = 0.25f});

  ASSERT_TRUE(items.ok()) << items.status();
  ASSERT_EQ(items->size(), 1u);
  const EntityRenderItem& item = items->front();
  EXPECT_EQ(item.entity_id, 7u);
  EXPECT_EQ(item.bounds.min, (Vec{90, 180}));
  EXPECT_EQ(item.bounds.max, (Vec{122, 228}));
  EXPECT_TRUE(item.show_border);
  EXPECT_TRUE(item.selected);
  EXPECT_FLOAT_EQ(item.overlay_opacity, 0.25f);
  ASSERT_TRUE(item.sprite.has_value());
  EXPECT_EQ(item.sprite->source.x, 8);
  EXPECT_EQ(item.sprite->source.y, 12);
  EXPECT_EQ(item.sprite->source.width, 16);
  EXPECT_EQ(item.sprite->source.height, 24);
}

TEST(ViewportSceneEntityTest, OmitsInactiveEntitiesAndCentersPlaceholderBounds) {
  std::map<uint64_t, Entity> entities{
      {1, Entity{.id = 1, .active = false, .transform = {.position = {10, 20}}}},
      {2, Entity{.id = 2, .transform = {.position = {100, 200}}}},
  };

  absl::StatusOr<std::vector<EntityRenderItem>> items =
      ComposeEntityRenderItems(entities, {});

  ASSERT_TRUE(items.ok()) << items.status();
  ASSERT_EQ(items->size(), 1u);
  EXPECT_EQ(items->front().entity_id, 2u);
  EXPECT_EQ(items->front().bounds.min, (Vec{84, 184}));
  EXPECT_EQ(items->front().bounds.max, (Vec{116, 216}));
  EXPECT_FALSE(items->front().sprite.has_value());
}

TEST(ViewportSceneEntityTest, SpriteWithoutTextureStillUsesSpriteBounds) {
  Sprite sprite{
      .frames = {SpriteFrame{.render_w = 20, .render_h = 30, .offset_x = -5, .offset_y = -10}},
  };
  std::map<uint64_t, Entity> entities{
      {1, Entity{.id = 1, .transform = {.position = {50, 60}}, .sprite = &sprite}},
  };

  absl::StatusOr<std::vector<EntityRenderItem>> items =
      ComposeEntityRenderItems(entities, {});

  ASSERT_TRUE(items.ok()) << items.status();
  ASSERT_EQ(items->size(), 1u);
  EXPECT_EQ(items->front().bounds.min, (Vec{45, 50}));
  EXPECT_EQ(items->front().bounds.max, (Vec{65, 80}));
  EXPECT_FALSE(items->front().sprite.has_value());
}

TEST(ViewportSceneEntityTest, RejectsInvalidState) {
  Sprite sprite{.frames = {SpriteFrame{.render_w = 0, .render_h = 16}}};
  std::map<uint64_t, Entity> entities{
      {1, Entity{.id = 1, .sprite = &sprite}},
  };

  EXPECT_EQ(ComposeEntityRenderItems(entities, {}).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ComposeEntityRenderItems(entities, {.overlay_opacity = 1.1f}).status().code(),
            absl::StatusCode::kInvalidArgument);

  int texture_owner = 0;
  sprite.frames.front() = SpriteFrame{
      .texture_w = 0,
      .texture_h = 16,
      .render_w = 16,
      .render_h = 16,
  };
  sprite.texture_handle = TextureHandleAccess::Create(1, &texture_owner);
  EXPECT_EQ(ComposeEntityRenderItems(entities, {}).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ViewportSceneZoneTest, CullsOffscreenZonesAndGivesSelectionPrecedence) {
  Camera camera{
      .position = {0, 0},
      .zoom = 1.0,
      .viewport_width = 100,
      .viewport_height = 100,
  };
  std::vector<ParallaxZone> zones{
      {.id = 1, .min_point = {-20, -20}, .max_point = {20, 20}},
      {.id = 2, .min_point = {100, 100}, .max_point = {150, 150}},
  };

  absl::StatusOr<std::vector<ZoneGizmoItem>> items =
      ComposeZoneGizmoItems(zones, camera, /*selected_zone_id=*/1, /*active_zone_id=*/1);

  ASSERT_TRUE(items.ok()) << items.status();
  ASSERT_EQ(items->size(), 1u);
  EXPECT_EQ(items->front().zone_id, 1);
  EXPECT_EQ(items->front().state, ZoneGizmoState::kSelected);
}

TEST(ViewportSceneZoneTest, RejectsInvalidCameraAndZoneBounds) {
  Camera invalid_camera;
  EXPECT_EQ(ComposeZoneGizmoItems({}, invalid_camera, std::nullopt, std::nullopt).status().code(),
            absl::StatusCode::kInvalidArgument);

  Camera camera{.zoom = 1.0, .viewport_width = 100, .viewport_height = 100};
  std::vector<ParallaxZone> invalid_zones{
      {.id = 1, .min_point = {20, 20}, .max_point = {10, 30}},
  };
  EXPECT_EQ(
      ComposeZoneGizmoItems(invalid_zones, camera, std::nullopt, std::nullopt).status().code(),
      absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
