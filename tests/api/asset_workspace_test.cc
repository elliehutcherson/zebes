#include "api/asset_workspace.h"

#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "api/api.h"
#include "common/config.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "platform/headless/headless_texture_store.h"

namespace zebes {
namespace {

constexpr char kAssetsRoot[] = ZEBES_TEST_ASSETS_DIR;

TEST(AssetWorkspaceTest, LevelReviewProfileLoadsRenderableCatalogsOnly) {
  EngineConfig config;
  HeadlessTextureStore texture_resources;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<AssetWorkspace> workspace,
                       AssetWorkspace::Create({
                           .config = &config,
                           .texture_resources = &texture_resources,
                           .asset_root = kAssetsRoot,
                           .load_profile = AssetWorkspace::LoadProfile::kLevelReview,
                       }));

  Api& api = workspace->api();
  ASSERT_OK_AND_ASSIGN(const std::vector<Texture> textures, api.GetAllTextures());
  EXPECT_FALSE(textures.empty());
  EXPECT_FALSE(api.GetAllSprites().empty());
  EXPECT_FALSE(api.GetAllLevels().empty());
  EXPECT_FALSE(api.GetAllParallaxThemes().empty());
  EXPECT_FALSE(api.GetAllTilesets().empty());

  EXPECT_TRUE(api.GetAllColliders().empty());
  EXPECT_TRUE(api.GetAllBlueprints().empty());
  EXPECT_TRUE(api.GetAllTerrainRecipes().empty());
  EXPECT_TRUE(api.GetAllSourceArtwork().empty());
  EXPECT_TRUE(api.GetAllPropRecipes().empty());
  EXPECT_TRUE(api.GetAllParallaxArtworkRecipes().empty());
}

TEST(AssetWorkspaceTest, LevelReviewProfileRejectsWriteAccessAndUnknownProfiles) {
  EngineConfig config;
  HeadlessTextureStore texture_resources;
  EXPECT_EQ(AssetWorkspace::Options{}.load_profile, AssetWorkspace::LoadProfile::kComplete);
  ASSERT_OK_AND_ASSIGN(const AssetWorkspace::LoadProfile complete,
                       AssetWorkspace::ParseLoadProfile("complete"));
  EXPECT_EQ(complete, AssetWorkspace::LoadProfile::kComplete);
  EXPECT_EQ(AssetWorkspace::LoadProfileId(complete), "complete");
  ASSERT_OK_AND_ASSIGN(const AssetWorkspace::LoadProfile parsed,
                       AssetWorkspace::ParseLoadProfile("referenced-level"));
  EXPECT_EQ(parsed, AssetWorkspace::LoadProfile::kLevelReview);
  EXPECT_EQ(AssetWorkspace::LoadProfileId(parsed), "referenced-level");
  EXPECT_TRUE(absl::IsInvalidArgument(AssetWorkspace::ParseLoadProfile("fast").status()));
  EXPECT_TRUE(absl::IsInvalidArgument(
      AssetWorkspace::Create({
                                 .config = &config,
                                 .texture_resources = &texture_resources,
                                 .asset_root = kAssetsRoot,
                                 .access = AssetWorkspace::Access::kReadWrite,
                                 .load_profile = AssetWorkspace::LoadProfile::kLevelReview,
                             })
          .status()));
  EXPECT_TRUE(absl::IsInvalidArgument(
      AssetWorkspace::Create({
                                 .config = &config,
                                 .texture_resources = &texture_resources,
                                 .asset_root = kAssetsRoot,
                                 .load_profile = static_cast<AssetWorkspace::LoadProfile>(99),
                             })
          .status()));
}

}  // namespace
}  // namespace zebes
