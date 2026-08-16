#include "resources/prop_recipe_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "artwork/prop_recipe.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"
#include "terrain/terrain_palette.h"
#include "terrain/terrain_style.h"

namespace zebes {
namespace {

using ::testing::HasSubstr;

PropRecipe CompleteRecipe() {
  TerrainGenConfig terrain;
  terrain.tile_size = 16;
  PropRecipe recipe;
  recipe.name = "Meadow tree";
  recipe.source_artwork_id = "source-1";
  recipe.terrain_recipe_id = "terrain-1";
  recipe.style.tile_size = 16;
  recipe.style.pixel_block_size = 2;
  recipe.style.palette = ResolveTerrainPalette(terrain).value();
  recipe.pipeline.source_limits.maximum_width = 2048;
  recipe.pipeline.source_limits.maximum_height = 2048;
  recipe.pipeline.source_limits.maximum_pixels = 4 * 1024 * 1024;
  recipe.pipeline.source_limits.maximum_bytes = 16 * 1024 * 1024;
  recipe.pipeline.isolation.alpha_threshold = 20;
  recipe.pipeline.isolation.background_distance = 32.0f;
  recipe.pipeline.isolation.enclosed_background_distance = 6.0f;
  recipe.pipeline.isolation.minimum_subject_area = 32;
  recipe.pipeline.isolation.competing_subject_ratio = 0.15f;
  recipe.pipeline.composition.canvas_tiles_wide = 3;
  recipe.pipeline.composition.canvas_tiles_high = 4;
  recipe.pipeline.composition.padding_fraction = 0.08f;
  recipe.pipeline.edge.width = 2;
  recipe.pipeline.edge.alpha_threshold = 120;
  recipe.pipeline.cleanup.alpha_threshold = 130;
  recipe.pipeline.cleanup.minimum_component_area = 3;
  recipe.pipeline.cleanup.grounded_tolerance = 2;
  recipe.texture_id = "texture-1";
  recipe.sprite_id = "sprite-1";
  recipe.blueprint_id = "blueprint-1";
  recipe.expected_frame = SpriteFrame{
      .index = 0,
      .texture_x = 0,
      .texture_y = 0,
      .texture_w = 48,
      .texture_h = 64,
      .render_w = 48,
      .render_h = 64,
      .frames_per_cycle = 0,
      .offset_x = -24,
      .offset_y = -63,
  };
  recipe.final_pixel_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  return recipe;
}

class PropRecipeManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            ("zebes-prop-recipes-" + std::to_string(++sequence));
    std::filesystem::remove_all(path_);
    ASSERT_OK_AND_ASSIGN(manager_, PropRecipeManager::Create(path_.string()));
    ASSERT_OK(manager_->LoadAllRecipes());
  }

  void TearDown() override { std::filesystem::remove_all(path_); }

  std::filesystem::path path_;
  std::unique_ptr<PropRecipeManager> manager_;
  static int sequence;
};

int PropRecipeManagerTest::sequence = 0;

TEST_F(PropRecipeManagerTest, RoundTripsEveryStyleAndPipelineField) {
  PropRecipe recipe = CompleteRecipe();
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateRecipe(recipe));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<PropRecipeManager> reloaded,
                       PropRecipeManager::Create(path_.string()));
  ASSERT_OK(reloaded->LoadAllRecipes());
  ASSERT_OK_AND_ASSIGN(PropRecipe * loaded, reloaded->GetRecipe(id));
  recipe.id = id;
  EXPECT_EQ(PropRecipeToJson(*loaded), PropRecipeToJson(recipe));
}

TEST_F(PropRecipeManagerTest, DetachedStyleWritesAnExplicitNullReference) {
  PropRecipe recipe = CompleteRecipe();
  recipe.terrain_recipe_id.reset();
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateRecipe(recipe));

  std::ifstream stream(path_ / "definitions/prop_recipes" / (id + ".json"));
  nlohmann::json json;
  stream >> json;
  EXPECT_TRUE(json.at("terrain_recipe_id").is_null());
}

TEST_F(PropRecipeManagerTest, RejectsMissingSettingsRatherThanUsingDefaults) {
  PropRecipe recipe = CompleteRecipe();
  recipe.id = "broken";
  nlohmann::json json = PropRecipeToJson(recipe);
  json["pipeline"]["cleanup"].erase("grounded_tolerance");
  const std::filesystem::path directory = path_ / "definitions/prop_recipes";
  std::ofstream(directory / "broken.json") << json.dump(2);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<PropRecipeManager> reloaded,
                       PropRecipeManager::Create(path_.string()));
  EXPECT_EQ(reloaded->LoadAllRecipes().code(), absl::StatusCode::kDataLoss);
}

TEST_F(PropRecipeManagerTest, RejectsUnknownSchemaAndNamesTheMigration) {
  PropRecipe recipe = CompleteRecipe();
  recipe.id = "future";
  nlohmann::json json = PropRecipeToJson(recipe);
  json["schema_version"] = kPropRecipeSchemaVersion + 1;
  const std::filesystem::path directory = path_ / "definitions/prop_recipes";
  std::ofstream(directory / "future.json") << json.dump(2);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<PropRecipeManager> reloaded,
                       PropRecipeManager::Create(path_.string()));
  const absl::Status status = reloaded->LoadAllRecipes();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(status.message()), HasSubstr("migrate_definitions.py"));
}

TEST_F(PropRecipeManagerTest, RejectsAFrameThatDoesNotMatchTheAuthoredCanvas) {
  PropRecipe recipe = CompleteRecipe();
  recipe.expected_frame.render_w = 47;
  const absl::Status status = manager_->CreateRecipe(recipe).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()), HasSubstr("1:1 static frame"));
}

TEST_F(PropRecipeManagerTest, SavingKeepsPointersStable) {
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateRecipe(CompleteRecipe()));
  ASSERT_OK_AND_ASSIGN(PropRecipe * held, manager_->GetRecipe(id));
  PropRecipe edited = *held;
  edited.name = "Renamed tree";
  ASSERT_OK(manager_->SaveRecipe(edited));

  ASSERT_OK_AND_ASSIGN(PropRecipe * after, manager_->GetRecipe(id));
  EXPECT_EQ(after, held);
  EXPECT_EQ(held->name, "Renamed tree");
}

}  // namespace
}  // namespace zebes
