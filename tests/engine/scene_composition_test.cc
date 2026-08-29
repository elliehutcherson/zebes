#include "engine/scene_composition.h"

#include <limits>
#include <map>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

TEST(SceneCompositionTest, ComposesEntitiesWithoutEditorPresentationState) {
  int texture_owner = 0;
  const TextureHandle texture = TextureHandleAccess::Create(1, &texture_owner);
  const Sprite sprite{
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
  };
  const SpriteLookup sprites{
      {"sprite", ResolvedSprite{.sprite = &sprite, .texture = texture}},
  };
  const std::map<uint64_t, Entity> entities{
      {7,
       Entity{
           .id = 7, .transform = {.position = {100, 200}}, .sort_order = 3, .sprite_id = "sprite"}},
      {8, Entity{.id = 8, .active = false, .sort_order = -1}},
  };

  ASSERT_OK_AND_ASSIGN(const std::vector<SceneEntityRenderItem> items,
                       ComposeSceneEntityRenderItems(entities, sprites));

  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].entity_id, 7u);
  EXPECT_EQ(items[0].sort_order, 3);
  EXPECT_EQ(items[0].bounds.min, (Vec{90, 180}));
  EXPECT_EQ(items[0].bounds.max, (Vec{122, 228}));
  EXPECT_EQ(items[0].origin, (Vec{100, 200}));
  ASSERT_TRUE(items[0].sprite.has_value());
  EXPECT_EQ(items[0].sprite->texture, texture);
  EXPECT_EQ(items[0].sprite->source.x, 8);
  EXPECT_EQ(items[0].sprite->source.height, 24);
}

TEST(SceneCompositionTest, ComposesOnlyVisibleLevelTiles) {
  Level level{
      .tile_render_width = 16,
      .tile_render_height = 16,
      .width = 2048,
      .height = 1024,
  };
  WorldLayer& layer = level.layers.front();
  layer.tile_chunks[ChunkKey(0, 0)].tiles[2 * TileChunk::kSize + 1] = 7;
  layer.tile_chunks[ChunkKey(2, 0)].tiles[2 * TileChunk::kSize + 6] = 999;
  const Tileset tileset{
      .tile_width = 16,
      .tile_height = 24,
      .tiles = {{.id = 7, .source_x = 32, .source_y = 48, .shape = TileShape::kFullBlock}},
  };
  const Camera camera{
      .position = {32, 40},
      .zoom = 1.0,
      .viewport_width = 128,
      .viewport_height = 128,
  };
  int texture_owner = 0;
  const TextureHandle texture = TextureHandleAccess::Create(5, &texture_owner);

  ASSERT_OK_AND_ASSIGN(const SceneTileRenderBatch batch,
                       ComposeSceneLevelTileRenderBatch(level, layer, tileset, texture, camera));

  EXPECT_EQ(batch.atlas_texture, texture);
  ASSERT_EQ(batch.items.size(), 1u);
  EXPECT_EQ(batch.items[0].tile_id, 7);
  EXPECT_EQ(batch.items[0].bounds.min, (Vec{16, 32}));
  EXPECT_EQ(batch.items[0].bounds.max, (Vec{32, 48}));
  EXPECT_EQ(batch.items[0].source.x, 32);
  EXPECT_EQ(batch.items[0].collision_shape, TileShape::kFullBlock);
}

TEST(SceneCompositionTest, BindsParallaxTexturesWithoutNativeTypes) {
  int texture_owner = 0;
  const TextureHandle texture = TextureHandleAccess::Create(9, &texture_owner);
  const ParallaxTheme theme{
      .layers = {{
          .name = "Near",
          .scroll_factor = {0.5, 0.5},
          .elements = {{.id = 4, .name = "Formation", .texture_id = "formation"}},
      }},
  };
  const Camera camera{.zoom = 1.0, .viewport_width = 800, .viewport_height = 600};
  const std::map<std::string, TextureHandle> textures{{"formation", texture}};

  ASSERT_OK_AND_ASSIGN(const SceneParallaxRenderBatch batch,
                       ComposeSceneParallaxRenderBatch(theme, camera, textures));

  ASSERT_EQ(batch.layers.size(), 1u);
  ASSERT_EQ(batch.layers[0].elements.size(), 1u);
  EXPECT_EQ(batch.layers[0].elements[0].element_id, 4);
  EXPECT_EQ(batch.layers[0].elements[0].texture, texture);
}

TEST(SceneCompositionTest, RejectsInvalidSharedGeometry) {
  const Entity entity{.transform = {.position = {std::numeric_limits<double>::infinity(), 0}}};
  EXPECT_TRUE(absl::IsInvalidArgument(ComposeSceneEntityRenderItem(1, entity, {}).status()));

  const Camera invalid_camera;
  EXPECT_TRUE(absl::IsInvalidArgument(
      ComposeSceneLevelTileRenderBatch(Level{}, WorldLayer{}, Tileset{}, {}, invalid_camera)
          .status()));
}

}  // namespace
}  // namespace zebes
