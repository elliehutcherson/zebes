#include "game/game_level_assets.h"

#include <cstddef>
#include <memory>

#include "absl/status/status.h"
#include "api/asset_workspace.h"
#include "common/config.h"
#include "game/game_scene.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/camera.h"
#include "platform/headless/headless_texture_store.h"

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
  ASSERT_OK_AND_ASSIGN(GameLevelAssets assets,
                       LoadGameLevelAssets(workspace->api(), kCatacombsProcessionalId));

  EXPECT_EQ(assets.level.name, "Catacombs Processional");
  EXPECT_TRUE(assets.tileset_texture);
  EXPECT_FALSE(assets.sprites.empty());
  EXPECT_EQ(assets.sprites.size(), assets.sprite_textures.size());
  EXPECT_FALSE(assets.parallax_themes.empty());
  EXPECT_FALSE(assets.parallax_textures.empty());

  const Camera camera{
      .position = assets.level.spawn_point,
      .zoom = 1.0,
      .viewport_width = 960,
      .viewport_height = 540,
  };
  ASSERT_OK_AND_ASSIGN(const GameSceneFrame frame, ComposeGameSceneFrame(assets, camera));

  ASSERT_TRUE(frame.environment.has_value());
  EXPECT_EQ(frame.parallax.size(), 1);
  EXPECT_EQ(frame.world_layers.size(), assets.level.layers.size());
  size_t tile_count = 0;
  size_t entity_count = 0;
  for (const GameWorldLayerFrame& layer : frame.world_layers) {
    tile_count += layer.tiles.items.size();
    entity_count += layer.entities.size();
  }
  EXPECT_GT(tile_count, 0);
  EXPECT_GT(entity_count, 0);

  EXPECT_TRUE(absl::IsNotFound(LoadGameLevelAssets(workspace->api(), "missing-level").status()));
}

}  // namespace
}  // namespace zebes
