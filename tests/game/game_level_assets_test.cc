#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "api/asset_workspace.h"
#include "common/config.h"
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
      if (entity.blueprint_id != kMousePlayerPlaceholderBlueprintId) continue;
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
                           .player_blueprint_id = std::string(kMousePlayerPlaceholderBlueprintId),
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

}  // namespace
}  // namespace zebes
