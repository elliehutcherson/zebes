#include "authoring/terrain_builder.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "api_mock.h"
#include "editor/terrain_editor/terrain_creation.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"
#include "objects/tileset.h"
#include "terrain/terrain_recipe.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;

TerrainGenConfig SmallConfig() {
  TerrainGenConfig config;
  config.tile_size = 8;
  config.supersample = 1;
  config.variant_period = 1;
  config.material.name = "Catacombs Masonry";
  return config;
}

nlohmann::json SpecJson(const std::string& name, const TerrainGenConfig& config) {
  return {
      {"schema_version", kTerrainBuildSpecSchemaVersion},
      {"name", name},
      {"config", TerrainGenConfigToJson(config)},
  };
}

TEST(TerrainBuilderTest, LoadsTheProductionCatacombsTerrainSpec) {
  ASSERT_OK_AND_ASSIGN(const TerrainBuildSpec spec,
                       ReadTerrainBuildSpec(std::filesystem::path(ZEBES_TEST_ASSETS_DIR) /
                                            "authoring" / "terrains" / "catacombs_masonry.json"));

  EXPECT_EQ(spec.name, "Catacombs Masonry");
  EXPECT_EQ(spec.config.tile_size, 32);
  EXPECT_EQ(spec.config.variant_period, 3);
  EXPECT_EQ(spec.config.interior.details.family, TerrainDetailSet::kNone);
  EXPECT_EQ(spec.config.interior.details.density, 0);
}

TEST(TerrainBuilderTest, RejectsUnknownSpecAndConfigFields) {
  nlohmann::json spec = SpecJson("Catacombs Masonry", SmallConfig());
  spec["unexpected"] = true;
  EXPECT_THAT(TerrainBuildSpecFromJson(spec).status().message(), HasSubstr("unknown field"));

  spec = SpecJson("Catacombs Masonry", SmallConfig());
  spec["config"]["material"]["unexpected"] = true;
  EXPECT_THAT(TerrainBuildSpecFromJson(spec).status().message(), HasSubstr("unknown field"));
}

TEST(TerrainBuilderTest, RejectsANameThatDiffersFromTheMaterialName) {
  nlohmann::json spec = SpecJson("Catacombs Masonry", SmallConfig());
  spec["config"]["material"]["name"] = "Some Other Material";

  const absl::Status status = TerrainBuildSpecFromJson(spec).status();

  EXPECT_TRUE(absl::IsInvalidArgument(status));
  EXPECT_THAT(status.message(), HasSubstr("does not match material name"));
}

TEST(TerrainBuilderTest, CreatesANewGeneratedBundleThroughTheProductionTransaction) {
  NiceMock<MockApi> api;
  EXPECT_CALL(api, GetAllTerrainRecipes()).WillOnce(Return(std::vector<TerrainRecipe>{}));
  EXPECT_CALL(api, CreateTextureFromPixels("Catacombs Masonry", _, _, _))
      .WillOnce(Return(std::string("texture-id")));
  EXPECT_CALL(api, CreateTileset(_)).WillOnce([](Tileset tileset) {
    EXPECT_EQ(tileset.name, "Catacombs Masonry");
    EXPECT_EQ(tileset.texture_id, "texture-id");
    EXPECT_EQ(tileset.terrains.size(), 1u);
    EXPECT_EQ(tileset.terrains.front().name, "Catacombs Masonry");
    return std::string("tileset-id");
  });
  EXPECT_CALL(api, CreateTerrainRecipe(_)).WillOnce([](TerrainRecipe recipe) {
    EXPECT_EQ(recipe.name, "Catacombs Masonry");
    EXPECT_EQ(recipe.tileset_id, "tileset-id");
    EXPECT_EQ(recipe.texture_id, "texture-id");
    return std::string("recipe-id");
  });

  ASSERT_OK_AND_ASSIGN(const TerrainBuildResult result,
                       BuildTerrain(api, {.name = "Catacombs Masonry", .config = SmallConfig()}));

  EXPECT_TRUE(result.created);
  EXPECT_EQ(result.recipe_id, "recipe-id");
  EXPECT_EQ(result.tileset_id, "tileset-id");
  EXPECT_EQ(result.texture_id, "texture-id");
  EXPECT_GT(result.tile_count, 0);
}

TEST(TerrainBuilderTest, RegeneratesTheUniqueExistingBundleWithoutChangingIds) {
  const TerrainGenConfig original = SmallConfig();
  ASSERT_OK_AND_ASSIGN(PreparedGeneratedTerrain prepared,
                       PrepareGeneratedTerrain("Catacombs Masonry", original));
  Tileset tileset{
      .id = "tileset-id",
      .name = "Catacombs Masonry",
      .texture_id = "texture-id",
      .tile_width = original.tile_size,
      .tile_height = original.tile_size,
      .tiles = std::move(prepared.candidate.tiles),
      .terrains = {std::move(prepared.candidate.terrain)},
  };
  tileset.terrains.front().name = "Catacombs Masonry";
  const TerrainRecipe recipe{
      .id = "recipe-id",
      .name = "Catacombs Masonry",
      .tileset_id = tileset.id,
      .texture_id = tileset.texture_id,
      .terrain_id = tileset.terrains.front().id,
      .config = original,
  };
  TerrainGenConfig edited = original;
  edited.seed = 9876;

  NiceMock<MockApi> api;
  EXPECT_CALL(api, GetAllTerrainRecipes()).WillOnce(Return(std::vector<TerrainRecipe>{recipe}));
  EXPECT_CALL(api, GetTileset(tileset.id)).Times(2).WillRepeatedly(Return(&tileset));
  EXPECT_CALL(api, SaveTerrainRecipe(_)).WillOnce([&](const TerrainRecipe& saved) {
    EXPECT_EQ(saved.id, recipe.id);
    EXPECT_EQ(saved.config.seed, edited.seed);
    return absl::OkStatus();
  });
  EXPECT_CALL(api, ReplaceTexturePixels(recipe.texture_id, _, _, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(api, CreateTextureFromPixels(_, _, _, _)).Times(0);
  EXPECT_CALL(api, CreateTileset(_)).Times(0);
  EXPECT_CALL(api, CreateTerrainRecipe(_)).Times(0);

  ASSERT_OK_AND_ASSIGN(const TerrainBuildResult result,
                       BuildTerrain(api, {.name = "Catacombs Masonry", .config = edited}));

  EXPECT_FALSE(result.created);
  EXPECT_EQ(result.recipe_id, recipe.id);
  EXPECT_EQ(result.tileset_id, recipe.tileset_id);
  EXPECT_EQ(result.texture_id, recipe.texture_id);
  EXPECT_EQ(result.tile_count, static_cast<int>(tileset.tiles.size()));
}

TEST(TerrainBuilderTest, RejectsDuplicateRecipeNamesBeforeWritingAnything) {
  const TerrainRecipe first{.id = "one", .name = "Catacombs Masonry"};
  const TerrainRecipe second{.id = "two", .name = "Catacombs Masonry"};
  NiceMock<MockApi> api;
  EXPECT_CALL(api, GetAllTerrainRecipes())
      .WillOnce(Return(std::vector<TerrainRecipe>{first, second}));
  EXPECT_CALL(api, CreateTextureFromPixels(_, _, _, _)).Times(0);
  EXPECT_CALL(api, ReplaceTexturePixels(_, _, _, _)).Times(0);

  const absl::Status status =
      BuildTerrain(api, {.name = "Catacombs Masonry", .config = SmallConfig()}).status();

  EXPECT_TRUE(absl::IsFailedPrecondition(status));
  EXPECT_THAT(status.message(), HasSubstr("more than one terrain recipe"));
}

TEST(TerrainBuilderTest, RejectsARenamedExistingBundleBeforeWritingAnything) {
  const TerrainRecipe recipe{
      .id = "recipe-id",
      .name = "Catacombs Masonry",
      .tileset_id = "tileset-id",
      .texture_id = "texture-id",
      .terrain_id = 1,
      .config = SmallConfig(),
  };
  Tileset tileset{
      .id = recipe.tileset_id,
      .name = "Renamed Tileset",
      .texture_id = recipe.texture_id,
  };
  NiceMock<MockApi> api;
  EXPECT_CALL(api, GetAllTerrainRecipes()).WillOnce(Return(std::vector<TerrainRecipe>{recipe}));
  EXPECT_CALL(api, GetTileset(recipe.tileset_id)).WillOnce(Return(&tileset));
  EXPECT_CALL(api, SaveTerrainRecipe(_)).Times(0);
  EXPECT_CALL(api, ReplaceTexturePixels(_, _, _, _)).Times(0);

  const absl::Status status =
      BuildTerrain(api, {.name = "Catacombs Masonry", .config = SmallConfig()}).status();

  EXPECT_TRUE(absl::IsFailedPrecondition(status));
  EXPECT_THAT(status.message(), HasSubstr("ambiguously renamed bundle"));
}

}  // namespace
}  // namespace zebes
