#include "editor/level_editor/viewport_scene.h"

#include <limits>
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
  EXPECT_EQ(item.mode, EntityRenderMode::kLevel);
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

TEST(ViewportSceneEntityTest, ComposesPlacementGhostWithSharedEntityGeometry) {
  int texture_owner = 0;
  Sprite sprite{
      .frames = {SpriteFrame{
          .texture_x = 4,
          .texture_y = 8,
          .texture_w = 16,
          .texture_h = 24,
          .render_w = 32,
          .render_h = 48,
          .offset_x = -10,
          .offset_y = -20,
      }},
      .texture_handle = TextureHandleAccess::Create(3, &texture_owner),
  };

  absl::StatusOr<EntityRenderItem> item =
      ComposeEntityPlacementItem({100, 200}, &sprite);

  ASSERT_TRUE(item.ok()) << item.status();
  EXPECT_EQ(item->mode, EntityRenderMode::kPlacementGhost);
  EXPECT_EQ(item->entity_id, Entity::kInvalidId);
  EXPECT_EQ(item->bounds.min, (Vec{90, 180}));
  EXPECT_EQ(item->bounds.max, (Vec{122, 228}));
  ASSERT_TRUE(item->sprite.has_value());
  EXPECT_EQ(item->sprite->texture, sprite.texture_handle);
  EXPECT_EQ(item->sprite->source.x, 4);
  EXPECT_EQ(item->sprite->source.y, 8);
}

TEST(ViewportSceneEntityTest, PlacementGhostAllowsNoSpriteButRejectsBrokenSprite) {
  absl::StatusOr<EntityRenderItem> placeholder =
      ComposeEntityPlacementItem({100, 200}, nullptr);
  ASSERT_TRUE(placeholder.ok()) << placeholder.status();
  EXPECT_EQ(placeholder->bounds.min, (Vec{84, 184}));
  EXPECT_EQ(placeholder->bounds.max, (Vec{116, 216}));
  EXPECT_FALSE(placeholder->sprite.has_value());

  Sprite missing_resource{
      .frames = {SpriteFrame{
          .texture_w = 16,
          .texture_h = 16,
          .render_w = 16,
          .render_h = 16,
      }},
  };
  EXPECT_EQ(ComposeEntityPlacementItem({0, 0}, &missing_resource).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(ComposeEntityPlacementItem(
                {std::numeric_limits<double>::infinity(), 0}, nullptr)
                .status()
                .code(),
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

TEST(ViewportSceneParallaxTest, BindsTexturesInAuthoredLayerOrder) {
  int texture_owner = 0;
  const TextureHandle forest = TextureHandleAccess::Create(1, &texture_owner);
  const TextureHandle fog = TextureHandleAccess::Create(2, &texture_owner);
  ParallaxTheme theme{
      .layers = {
          {.name = "Incomplete", .scroll_factor = {1, 1}},
          {.name = "Forest", .texture_id = "forest", .scroll_factor = {0.5, 0.5}},
          {.name = "Fog", .texture_id = "fog", .scroll_factor = {0.8, 0.8}},
          {.name = "Forest Front", .texture_id = "forest", .scroll_factor = {1, 1}},
      },
  };
  Camera camera{.zoom = 1.0, .viewport_width = 800, .viewport_height = 600};
  std::map<std::string, TextureHandle> textures{
      {"fog", fog},
      {"forest", forest},
  };

  absl::StatusOr<ParallaxRenderBatch> batch =
      ComposeParallaxRenderBatch(theme, camera, textures);

  ASSERT_TRUE(batch.ok()) << batch.status();
  ASSERT_EQ(batch->layers.size(), 3u);
  EXPECT_EQ(batch->layers[0].layer.name, "Forest");
  EXPECT_EQ(batch->layers[0].texture, forest);
  EXPECT_EQ(batch->layers[1].layer.name, "Fog");
  EXPECT_EQ(batch->layers[1].texture, fog);
  EXPECT_EQ(batch->layers[2].layer.name, "Forest Front");
  EXPECT_EQ(batch->layers[2].texture, forest);
}

TEST(ViewportSceneParallaxTest, RejectsMissingTexturesAndInvalidGeometry) {
  Camera camera{.zoom = 1.0, .viewport_width = 800, .viewport_height = 600};
  ParallaxTheme theme{
      .layers = {{.texture_id = "missing", .scroll_factor = {1, 1}}},
  };
  EXPECT_EQ(ComposeParallaxRenderBatch(theme, camera, {}).status().code(),
            absl::StatusCode::kFailedPrecondition);

  int texture_owner = 0;
  std::map<std::string, TextureHandle> textures{
      {"missing", TextureHandleAccess::Create(1, &texture_owner)},
  };
  theme.layers.front().base_scale = 0.0f;
  EXPECT_EQ(ComposeParallaxRenderBatch(theme, camera, textures).status().code(),
            absl::StatusCode::kInvalidArgument);

  camera.zoom = 0.0;
  EXPECT_EQ(ComposeParallaxRenderBatch(theme, camera, textures).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ViewportSceneTileTest, ComposesOnlyVisibleTilesWithAtlasAndPresentationState) {
  Level level{
      .tile_render_width = 16,
      .tile_render_height = 16,
      .width = 2048,
      .height = 1024,
  };
  ASSERT_TRUE(SetTileAt(level, 1, 2, 7).ok());
  ASSERT_TRUE(SetTileAt(level, 70, 2, 7).ok());
  Tileset tileset{
      .tile_width = 16,
      .tile_height = 24,
      .tiles = {{.id = 7,
                 .source_x = 32,
                 .source_y = 48,
                 .shape = TileShape::kFullBlock}},
  };
  Camera camera{
      .position = {32, 40},
      .zoom = 1.0,
      .viewport_width = 128,
      .viewport_height = 128,
  };
  int texture_owner = 0;
  const TextureHandle texture = TextureHandleAccess::Create(5, &texture_owner);

  absl::StatusOr<TileRenderBatch> batch = ComposeLevelTileRenderBatch(
      level, tileset, texture, camera,
      {.overlay_opacity = 0.5f, .show_frame = true, .show_collision = true});

  ASSERT_TRUE(batch.ok()) << batch.status();
  EXPECT_EQ(batch->atlas_texture, texture);
  EXPECT_EQ(batch->mode, TileRenderMode::kLevel);
  EXPECT_FLOAT_EQ(batch->overlay_opacity, 0.5f);
  EXPECT_TRUE(batch->show_frame);
  EXPECT_TRUE(batch->show_collision);
  ASSERT_EQ(batch->items.size(), 1u);
  const TileRenderItem& item = batch->items.front();
  EXPECT_EQ(item.tile_id, 7);
  EXPECT_EQ(item.bounds.min, (Vec{16, 32}));
  EXPECT_EQ(item.bounds.max, (Vec{32, 48}));
  EXPECT_EQ(item.source.x, 32);
  EXPECT_EQ(item.source.y, 48);
  EXPECT_EQ(item.source.width, 16);
  EXPECT_EQ(item.source.height, 24);
  EXPECT_EQ(item.collision_shape, TileShape::kFullBlock);
}

TEST(ViewportSceneTileTest, OffscreenChunksAreNotScanned) {
  Level level{
      .tile_render_width = 16,
      .tile_render_height = 16,
      .width = 4096,
      .height = 1024,
  };
  // Unknown tile data is deliberately placed in a distant chunk. The camera
  // only composes the visible chunk, avoiding 1,024-cell scans elsewhere.
  ASSERT_TRUE(SetTileAt(level, 100, 2, 999).ok());
  Tileset tileset{.tiles = {{.id = 1, .source_x = 0, .source_y = 0}}};
  Camera camera{.position = {32, 32}, .zoom = 1, .viewport_width = 64, .viewport_height = 64};

  absl::StatusOr<TileRenderBatch> batch =
      ComposeLevelTileRenderBatch(level, tileset, {}, camera, {});

  ASSERT_TRUE(batch.ok()) << batch.status();
  EXPECT_TRUE(batch->items.empty());
}

TEST(ViewportSceneTileTest, RejectsUnknownVisibleTileAndDuplicateDefinitions) {
  Level level{
      .tile_render_width = 16,
      .tile_render_height = 16,
      .width = 128,
      .height = 128,
  };
  ASSERT_TRUE(SetTileAt(level, 1, 1, 9).ok());
  Camera camera{.position = {32, 32}, .zoom = 1, .viewport_width = 64, .viewport_height = 64};
  Tileset tileset{.tiles = {{.id = 1}}};

  EXPECT_EQ(ComposeLevelTileRenderBatch(level, tileset, {}, camera, {}).status().code(),
            absl::StatusCode::kInvalidArgument);

  tileset.tiles.push_back(Tile{.id = 1});
  EXPECT_EQ(ComposeLevelTileRenderBatch(level, tileset, {}, camera, {}).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ViewportSceneTileTest, RejectsTileOutsideLevelBounds) {
  Level level{
      .tile_render_width = 16,
      .tile_render_height = 16,
      .width = 16,
      .height = 16,
  };
  ASSERT_TRUE(SetTileAt(level, 1, 0, 1).ok());
  Tileset tileset{.tiles = {{.id = 1}}};
  Camera camera{.position = {16, 8}, .zoom = 1, .viewport_width = 64, .viewport_height = 64};

  EXPECT_EQ(ComposeLevelTileRenderBatch(level, tileset, {}, camera, {}).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ViewportSceneTileTest, PlacementSnapsToRenderGrid) {
  Tileset tileset{
      .tile_width = 8,
      .tile_height = 12,
      .tiles = {{.id = 4, .source_x = 24, .source_y = 36}},
  };
  Tile tile{.id = 4, .source_x = 24, .source_y = 36};

  absl::StatusOr<TileRenderBatch> batch =
      ComposeTilePlacementBatch(tile, tileset, {}, {33, 47}, 16, 24);

  ASSERT_TRUE(batch.ok()) << batch.status();
  EXPECT_EQ(batch->mode, TileRenderMode::kPlacementGhost);
  ASSERT_EQ(batch->items.size(), 1u);
  EXPECT_EQ(batch->items.front().bounds.min, (Vec{32, 24}));
  EXPECT_EQ(batch->items.front().bounds.max, (Vec{48, 48}));
  EXPECT_EQ(batch->items.front().source.width, 8);
  EXPECT_EQ(batch->items.front().source.height, 12);
}

TEST(ViewportSceneTileTest, RejectsInvalidDimensionsAndOpacity) {
  Level level{.tile_render_width = 0, .tile_render_height = 16};
  Tileset tileset{.tiles = {{.id = 1}}};
  Camera camera{.zoom = 1, .viewport_width = 64, .viewport_height = 64};

  EXPECT_EQ(ComposeLevelTileRenderBatch(level, tileset, {}, camera, {}).status().code(),
            absl::StatusCode::kInvalidArgument);

  level.tile_render_width = 16;
  EXPECT_EQ(ComposeLevelTileRenderBatch(level, tileset, {}, camera,
                                        {.overlay_opacity = -0.1f})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ComposeLevelTileRenderBatch(
                level, tileset, {}, camera,
                {.overlay_opacity = std::numeric_limits<float>::quiet_NaN()})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);

  camera.position.x = std::numeric_limits<double>::infinity();
  EXPECT_EQ(ComposeLevelTileRenderBatch(level, tileset, {}, camera, {}).status().code(),
            absl::StatusCode::kInvalidArgument);

  const Tile& tile = tileset.tiles.front();
  EXPECT_EQ(ComposeTilePlacementBatch(tile, tileset, {},
                                      {std::numeric_limits<double>::infinity(), 0}, 16, 16)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ComposeTilePlacementBatch(tile, tileset, {},
                                      {std::numeric_limits<double>::max(), 0}, 16, 16)
                .status()
                .code(),
            absl::StatusCode::kOutOfRange);

  camera.position.x = 0;
  level.width = std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ(ComposeLevelTileRenderBatch(level, tileset, {}, camera, {}).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
