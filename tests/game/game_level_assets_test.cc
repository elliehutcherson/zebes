#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "api/asset_workspace.h"
#include "common/config.h"
#include "engine/texture_handle.h"
#include "game/game_scene.h"
#include "game/runtime_world.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/camera.h"
#include "objects/collider.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "platform/headless/headless_texture_store.h"
#include "resources/loaded_level_assets.h"

namespace zebes {
namespace {

constexpr char kAssetsRoot[] = ZEBES_TEST_ASSETS_DIR;
constexpr char kCatacombsProcessionalId[] = "9e20ee58-f4d2-4931-b74b-5555d4b35c00";

TEST(GameLevelAssetsTest, RuntimeProfileLoadsAndComposesTheShippedInitialLevel) {
  EngineConfig config;
  HeadlessTextureStore texture_store;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<AssetWorkspace> workspace,
                       AssetWorkspace::Create({
                           .config = &config,
                           .texture_resources = &texture_store,
                           .asset_root = kAssetsRoot,
                           .load_profile = AssetWorkspace::LoadProfile::kRuntime,
                       }));
  ASSERT_OK_AND_ASSIGN(LoadedLevelAssets assets,
                       workspace->LoadLevelAssets(kCatacombsProcessionalId));

  EXPECT_EQ(assets.content.level.name, "Catacombs Processional");
  EXPECT_TRUE(assets.rendering.tileset_atlas);
  EXPECT_FALSE(assets.content.sprites.empty());
  EXPECT_EQ(assets.content.sprites.size(), assets.rendering.sprite_textures.size());
  const Entity* mouse_player = nullptr;
  for (const WorldLayer& layer : assets.content.level.layers) {
    for (const auto& entry : layer.entities) {
      const Entity& entity = entry.second;
      if (entity.blueprint_id != kPlayerBlueprintId) continue;
      ASSERT_EQ(mouse_player, nullptr) << "shipped level contains multiple mouse players";
      mouse_player = &entity;
    }
  }
  ASSERT_NE(mouse_player, nullptr);
  const auto mouse_collider = assets.content.colliders.find(mouse_player->collider_id);
  ASSERT_NE(mouse_collider, assets.content.colliders.end());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world,
                       RuntimeWorld::Create({
                           .level = assets.content.level,
                           .tileset = assets.content.tileset,
                           .blueprints = assets.content.blueprints,
                           .sprites = assets.content.sprites,
                           .player_blueprint_id = std::string(kPlayerBlueprintId),
                           .player_collider = mouse_collider->second,
                       }));
  EXPECT_EQ(world->player_entity_id(), 4);
  EXPECT_EQ(world->player_local_collider(),
            (AxisAlignedBox{.min = {-16.0, -64.0}, .max = {16.0, 0.0}}));
  EXPECT_FALSE(assets.content.parallax_themes.empty());
  EXPECT_FALSE(assets.rendering.parallax_textures.empty());

  const Camera camera{
      .position = assets.content.level.spawn_point,
      .zoom = 1.0,
      .viewport_width = 960,
      .viewport_height = 540,
  };
  ASSERT_OK_AND_ASSIGN(const GameSceneFrame frame, ComposeGameSceneFrame(assets, camera));

  ASSERT_TRUE(frame.environment.has_value());
  EXPECT_EQ(frame.parallax.size(), 1);
  EXPECT_EQ(frame.world_layers.size(), assets.content.level.layers.size());
  size_t tile_count = 0;
  size_t entity_count = 0;
  for (const GameWorldLayerFrame& layer : frame.world_layers) {
    tile_count += layer.tiles.items.size();
    entity_count += layer.entities.size();
  }
  EXPECT_GT(tile_count, 0);
  EXPECT_GT(entity_count, 0);

  EXPECT_TRUE(absl::IsNotFound(workspace->LoadLevelAssets("missing-level").status()));
}

TEST(GameLevelAssetsTest, RuntimePresentationOverridesReachTheFinalGameFrame) {
  int texture_owner = 0;
  const TextureHandle atlas = TextureHandleAccess::Create(1, &texture_owner);
  const TextureHandle authored_texture = TextureHandleAccess::Create(2, &texture_owner);
  const TextureHandle runtime_texture = TextureHandleAccess::Create(3, &texture_owner);
  LoadedLevelAssets assets{
      .content = {.level = {.id = "level",
                            .tileset_id = "tileset",
                            .tile_render_width = 16,
                            .tile_render_height = 16,
                            .width = 320,
                            .height = 240},
                  .tileset = {.id = "tileset", .tile_width = 16, .tile_height = 16},
                  .sprites = {{"authored", Sprite{.id = "authored",
                                                  .frames = {{.render_w = 8, .render_h = 8}}}},
                              {"runtime", Sprite{.id = "runtime",
                                                 .frames = {{.render_w = 10, .render_h = 12},
                                                            {.texture_x = 20,
                                                             .texture_y = 30,
                                                             .texture_w = 40,
                                                             .texture_h = 50,
                                                             .render_w = 60,
                                                             .render_h = 70,
                                                             .offset_x = 4,
                                                             .offset_y = 6}}}}}},
      .rendering = {.tileset_atlas = atlas,
                    .sprite_textures = {{"authored", authored_texture},
                                        {"runtime", runtime_texture}}},
  };
  assets.content.level.layers.front().entities.emplace(
      7, Entity{.id = 7, .transform = {.position = {100, 120}}, .sprite_id = "authored"});
  const Camera camera{
      .position = {160, 120},
      .zoom = 1.0,
      .viewport_width = 320,
      .viewport_height = 240,
  };
  const absl::flat_hash_map<uint64_t, std::string> sprite_ids{{7, "runtime"}};
  const absl::flat_hash_map<uint64_t, int> frame_indices{{7, 1}};

  ASSERT_OK_AND_ASSIGN(const GameSceneFrame frame,
                       ComposeGameSceneFrame(assets, camera,
                                             {.sprite_id_overrides = &sprite_ids,
                                              .frame_index_overrides = &frame_indices}));

  ASSERT_EQ(frame.world_layers.size(), 1u);
  ASSERT_EQ(frame.world_layers.front().entities.size(), 1u);
  const SceneEntityRenderItem& item = frame.world_layers.front().entities.front();
  ASSERT_TRUE(item.sprite.has_value());
  EXPECT_EQ(item.sprite->texture, runtime_texture);
  EXPECT_EQ(item.sprite->source.x, 20);
  EXPECT_EQ(item.sprite->source.y, 30);
  EXPECT_EQ(item.sprite->source.width, 40);
  EXPECT_EQ(item.sprite->source.height, 50);
  EXPECT_EQ(item.bounds.min, (Vec{104, 126}));
  EXPECT_EQ(item.bounds.max, (Vec{164, 196}));
}

}  // namespace
}  // namespace zebes
