#include "resources/terrain_recipe_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

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
            ("zebes-terrain-recipes-" + std::to_string(++sequence_));
    std::filesystem::remove_all(path_);
    ASSERT_OK_AND_ASSIGN(manager_, TerrainRecipeManager::Create(path_.string()));
    ASSERT_TRUE(manager_->LoadAllRecipes().ok());
  }

  void TearDown() override { std::filesystem::remove_all(path_); }

  static int sequence_;
  std::filesystem::path path_;
  std::unique_ptr<TerrainRecipeManager> manager_;
};

int TerrainRecipeManagerTest::sequence_ = 0;

TEST_F(TerrainRecipeManagerTest, RoundTripsEveryConfigurationField) {
  TerrainRecipe recipe = CompleteRecipe();
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateRecipe(recipe));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TerrainRecipeManager> reloaded,
                       TerrainRecipeManager::Create(path_.string()));
  ASSERT_TRUE(reloaded->LoadAllRecipes().ok());
  ASSERT_OK_AND_ASSIGN(TerrainRecipe * loaded, reloaded->GetRecipe(id));

  recipe.id = id;
  EXPECT_EQ(TerrainRecipeToJson(*loaded), TerrainRecipeToJson(recipe));
}

TEST_F(TerrainRecipeManagerTest, MigratesV1FacingBiasWithoutChangingItsCoverageLine) {
  TerrainRecipe recipe = CompleteRecipe();
  recipe.id = "legacy";
  nlohmann::json json = TerrainRecipeToJson(recipe);
  json["schema_version"] = 1;
  json["config"]["surface"] = {
      {"grass_band", 8.0f},       {"ruffle_amplitude", 2.0f}, {"ruffle_density", 2.5f},
      {"ruffle_sharpness", 0.4f}, {"ruffle_octaves", 2},      {"grass_bottom_bias", 0.25f},
      {"outline_width", 2},       {"grass_hi_depth", 1},      {"grass_shade_depth", 2},
      {"contact_depth", 1},       {"texture_size", 3.5f},     {"texture_amount", 0.6f},
  };

  ASSERT_OK_AND_ASSIGN(TerrainRecipe migrated, TerrainRecipeFromJson(json));
  EXPECT_FLOAT_EQ(migrated.config.surface.top_depth, 8.0f);
  EXPECT_FLOAT_EQ(migrated.config.surface.side_depth, 5.0f);
  EXPECT_FLOAT_EQ(migrated.config.surface.underside_depth, 2.0f);
  EXPECT_EQ(migrated.config.surface.wall_depth, 0);

  // Saving upgrades the document once. Reading that v3 document again must be
  // lossless, rather than repeatedly applying migration math.
  const nlohmann::json upgraded = TerrainRecipeToJson(migrated);
  EXPECT_EQ(upgraded.at("schema_version"), 3);
  EXPECT_FALSE(upgraded.at("config").at("surface").contains("grass_bottom_bias"));
  ASSERT_OK_AND_ASSIGN(TerrainRecipe reopened, TerrainRecipeFromJson(upgraded));
  EXPECT_EQ(TerrainRecipeToJson(reopened), upgraded);
}

TEST_F(TerrainRecipeManagerTest, MigratesV2WithEdgeDetailsDisabled) {
  TerrainRecipe recipe = CompleteRecipe();
  recipe.id = "phase-two";
  nlohmann::json json = TerrainRecipeToJson(recipe);
  json["schema_version"] = 2;
  json["config"]["surface"].erase("edge_detail");

  ASSERT_OK_AND_ASSIGN(TerrainRecipe migrated, TerrainRecipeFromJson(json));
  EXPECT_EQ(migrated.config.surface.edge_detail.family, TerrainEdgeDetailSet::kNone);
  EXPECT_EQ(TerrainRecipeToJson(migrated).at("schema_version"), 3);
}

TEST_F(TerrainRecipeManagerTest, RejectsInvalidV1FacingBiasDuringMigration) {
  TerrainRecipe recipe = CompleteRecipe();
  recipe.id = "legacy-broken";
  nlohmann::json json = TerrainRecipeToJson(recipe);
  json["schema_version"] = 1;
  json["config"]["surface"] = {
      {"grass_band", 8.0f},       {"ruffle_amplitude", 2.0f}, {"ruffle_density", 2.5f},
      {"ruffle_sharpness", 0.4f}, {"ruffle_octaves", 2},      {"grass_bottom_bias", 1.25f},
      {"outline_width", 2},       {"grass_hi_depth", 1},      {"grass_shade_depth", 2},
      {"contact_depth", 1},       {"texture_size", 3.5f},     {"texture_amount", 0.6f},
  };

  EXPECT_EQ(TerrainRecipeFromJson(json).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TerrainRecipeManagerTest, SavingAnEditReplacesTheRecipeWithoutChangingItsId) {
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateRecipe(CompleteRecipe()));
  ASSERT_OK_AND_ASSIGN(TerrainRecipe * recipe, manager_->GetRecipe(id));
  TerrainRecipe edited = *recipe;
  edited.config.seed = 42;
  edited.name = "Renamed";
  ASSERT_TRUE(manager_->SaveRecipe(edited).ok());

  ASSERT_OK_AND_ASSIGN(TerrainRecipe * saved, manager_->GetRecipe(id));
  EXPECT_EQ(saved->id, id);
  EXPECT_EQ(saved->name, "Renamed");
  EXPECT_EQ(saved->config.seed, 42u);
  EXPECT_TRUE(std::filesystem::exists(path_ / "definitions/terrain_recipes" / (id + ".json")));
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
  EXPECT_EQ(reloaded->LoadAllRecipes().code(), absl::StatusCode::kFailedPrecondition);
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
  EXPECT_EQ(reloaded->LoadAllRecipes().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
