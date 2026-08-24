#include "resources/terrain_recipe_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

TerrainRecipe CompleteRecipe() {
  TerrainRecipe recipe;
  recipe.name = "Cozy Meadow";
  recipe.tileset_id = "tileset-1";
  recipe.texture_id = "texture-1";
  recipe.terrain_id = 7;
  recipe.source_preset = "Classic Grass";
  recipe.config.tile_size = 16;
  recipe.config.supersample = 2;
  recipe.config.variant_period = 2;
  recipe.config.pixel_profile = TerrainPixelProfile::kChunky16;
  recipe.config.surface.top_depth = 4.25f;
  recipe.config.surface.side_depth = 2.75f;
  recipe.config.surface.underside_depth = 1.25f;
  recipe.config.surface.ruffle_amplitude = 1.5f;
  recipe.config.surface.ruffle_density = 2.5f;
  recipe.config.surface.ruffle_sharpness = 0.4f;
  recipe.config.surface.ruffle_octaves = 2;
  recipe.config.surface.outline_depth = 2;
  recipe.config.surface.highlight_depth = 1;
  recipe.config.surface.shade_depth = 2;
  recipe.config.surface.contact_depth = 1;
  recipe.config.surface.wall_depth = 3;
  recipe.config.surface.wall_darkness = 1.7f;
  recipe.config.surface.texture_size = 3.5f;
  recipe.config.surface.texture_amount = 0.6f;
  recipe.config.surface.edge_detail.family = TerrainEdgeDetailSet::kDryGrass;
  recipe.config.surface.edge_detail.amount = 0.75f;
  recipe.config.surface.edge_detail.length = 5;
  recipe.config.surface.edge_detail.clump_size = 6;
  recipe.config.surface.edge_detail.lean = 0.4f;
  recipe.config.surface.edge_detail.highlight = 0.2f;
  recipe.config.interior.base.style = TerrainInteriorStyle::kSoilClods;
  recipe.config.interior.base.mottle_density = 3.0f;
  recipe.config.interior.base.mottle_coverage = 0.2f;
  recipe.config.interior.base.feature_size = 5.0f;
  recipe.config.interior.base.relief = 0.7f;
  recipe.config.interior.pattern.family = TerrainSubstratePattern::kDiamonds;
  recipe.config.interior.pattern.density = 3;
  recipe.config.interior.pattern.spacing = 5;
  recipe.config.interior.pattern.margin = 1;
  recipe.config.interior.pattern.contrast = 0.8f;
  recipe.config.interior.pattern.scale = 2;
  recipe.config.interior.pattern.accent_mode = TerrainAccentMode::kGradient;
  recipe.config.interior.details.family = TerrainDetailSet::kCrystals;
  recipe.config.interior.details.density = 2;
  recipe.config.interior.details.spacing = 6;
  recipe.config.interior.details.margin = 1;
  recipe.config.interior.details.scale = 2;
  recipe.config.interior.details.accent_mode = TerrainAccentMode::kAccent;
  recipe.config.seed = 98421;
  recipe.config.material.name = "Gem Meadow";
  recipe.config.material.surface = 0xaabbcc;
  recipe.config.material.substrate = 0x112233;
  recipe.config.material.outline = 0x101010;
  recipe.config.material.accent_primary = 0xff00ff;
  recipe.config.material.accent_secondary = 0x00ffff;
  recipe.config.material.hue_shift = 0.02f;
  recipe.config.material.contrast = 0.9f;
  recipe.config.material.surface_style = TerrainSurfaceStyle::kScalloped;
  return recipe;
}

class TerrainRecipeManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            ("zebes-terrain-recipes-" + std::to_string(++sequence));
    std::filesystem::remove_all(path_);
    ASSERT_OK_AND_ASSIGN(manager_, TerrainRecipeManager::Create(path_.string()));
    ASSERT_OK(manager_->LoadAllRecipes());
  }

  void TearDown() override { std::filesystem::remove_all(path_); }

  static int sequence;
  std::filesystem::path path_;
  std::unique_ptr<TerrainRecipeManager> manager_;
};

int TerrainRecipeManagerTest::sequence = 0;

TEST_F(TerrainRecipeManagerTest, RoundTripsEveryConfigurationField) {
  TerrainRecipe recipe = CompleteRecipe();
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateRecipe(recipe));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TerrainRecipeManager> reloaded,
                       TerrainRecipeManager::Create(path_.string()));
  ASSERT_OK(reloaded->LoadAllRecipes());
  ASSERT_OK_AND_ASSIGN(TerrainRecipe * loaded, reloaded->GetRecipe(id));

  recipe.id = id;
  EXPECT_EQ(TerrainRecipeToJson(*loaded), TerrainRecipeToJson(recipe));
}

// Only the current version is read. An older document is brought forward by
// scripts/migrate_definitions.py, so the parser has exactly one shape rather
// than one per version that has ever existed. Refusing by name is what points
// the author at the migration instead of leaving them with a parse error.
TEST_F(TerrainRecipeManagerTest, RejectsASupersededSchemaAndNamesTheMigration) {
  for (const int superseded : {1, 2}) {
    TerrainRecipe recipe = CompleteRecipe();
    recipe.id = "legacy";
    nlohmann::json json = TerrainRecipeToJson(recipe);
    json["schema_version"] = superseded;

    absl::StatusOr<TerrainRecipe> parsed = TerrainRecipeFromJson(json);

    ASSERT_FALSE(parsed.ok()) << "schema version " << superseded;
    EXPECT_EQ(parsed.status().code(), absl::StatusCode::kFailedPrecondition);
    EXPECT_THAT(std::string(parsed.status().message()),
                ::testing::HasSubstr("migrate_definitions.py"));
  }
}

TEST_F(TerrainRecipeManagerTest, SavingAnEditReplacesTheRecipeWithoutChangingItsId) {
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateRecipe(CompleteRecipe()));
  ASSERT_OK_AND_ASSIGN(TerrainRecipe * recipe, manager_->GetRecipe(id));
  TerrainRecipe edited = *recipe;
  edited.config.seed = 42;
  edited.name = "Renamed";
  ASSERT_OK(manager_->SaveRecipe(edited));

  ASSERT_OK_AND_ASSIGN(TerrainRecipe * saved, manager_->GetRecipe(id));
  EXPECT_EQ(saved->id, id);
  EXPECT_EQ(saved->name, "Renamed");
  EXPECT_EQ(saved->config.seed, 42u);
  EXPECT_TRUE(std::filesystem::exists(path_ / "definitions/terrain_recipes" / (id + ".json")));
}

// The terrain editor holds a TerrainRecipe* across a regenerate, which saves.
// Replacing the cached unique_ptr freed what it held.
TEST_F(TerrainRecipeManagerTest, SavingKeepsPointersHandedOutBeforeIt) {
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateRecipe(CompleteRecipe()));
  ASSERT_OK_AND_ASSIGN(TerrainRecipe * held, manager_->GetRecipe(id));

  TerrainRecipe edited = *held;
  edited.name = "Renamed";
  ASSERT_OK(manager_->SaveRecipe(edited));

  ASSERT_OK_AND_ASSIGN(TerrainRecipe * after, manager_->GetRecipe(id));
  EXPECT_EQ(after, held) << "the address a caller is still holding must survive a save";
  EXPECT_EQ(held->name, "Renamed");
}

TEST_F(TerrainRecipeManagerTest, RejectsAnUnknownFutureSchema) {
  TerrainRecipe recipe = CompleteRecipe();
  recipe.id = "future";
  nlohmann::json json = TerrainRecipeToJson(recipe);
  json["schema_version"] = kTerrainRecipeSchemaVersion + 1;

  const std::filesystem::path directory = path_ / "definitions/terrain_recipes";
  std::filesystem::create_directories(directory);
  std::ofstream(directory / "future.json") << json.dump();

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TerrainRecipeManager> reloaded,
                       TerrainRecipeManager::Create(path_.string()));
  const absl::Status status = reloaded->LoadAllRecipes();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(status.message()), ::testing::HasSubstr("migrate_definitions.py"));
}

TEST_F(TerrainRecipeManagerTest, RejectsMissingConfigurationInsteadOfUsingDefaults) {
  TerrainRecipe recipe = CompleteRecipe();
  recipe.id = "broken";
  nlohmann::json json = TerrainRecipeToJson(recipe);
  json["config"]["interior"]["pattern"].erase("scale");

  const std::filesystem::path directory = path_ / "definitions/terrain_recipes";
  std::filesystem::create_directories(directory);
  std::ofstream(directory / "broken.json") << json.dump();

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TerrainRecipeManager> reloaded,
                       TerrainRecipeManager::Create(path_.string()));
  const absl::Status status = reloaded->LoadAllRecipes();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(status.message()), ::testing::HasSubstr("scale"));
}

}  // namespace
}  // namespace zebes
