#include "resources/parallax_artwork_recipe_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "artwork/parallax_artwork_recipe.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

ParallaxArtworkRecipe CompleteRecipe(std::string name = "Far Cave Plate") {
  return {
      .name = std::move(name),
      .source_artwork_id = "source-1",
      .terrain_recipe_id = "terrain-1",
      .style = {.pixel_block_size = 2,
                .quantize_to_palette = true,
                .palette = {RgbaColor{10, 16, 59, 255}, RgbaColor{81, 80, 126, 255}}},
      .pipeline = {.target_width = 8, .target_height = 4},
      .texture_id = "texture-1",
      .expected_width = 8,
      .expected_height = 4,
      .final_pixel_digest = std::string(64, 'a'),
  };
}

class ParallaxArtworkRecipeManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            ("zebes-parallax-artwork-recipes-" + std::to_string(++sequence));
    std::filesystem::remove_all(path_);
    ASSERT_OK_AND_ASSIGN(manager_, ParallaxArtworkRecipeManager::Create(path_.string()));
    ASSERT_OK(manager_->LoadAllRecipes());
  }

  void TearDown() override { std::filesystem::remove_all(path_); }

  std::filesystem::path path_;
  std::unique_ptr<ParallaxArtworkRecipeManager> manager_;
  static int sequence;
};

int ParallaxArtworkRecipeManagerTest::sequence = 0;

TEST_F(ParallaxArtworkRecipeManagerTest, RoundTripsEveryStyleAndPipelineField) {
  ParallaxArtworkRecipe recipe = CompleteRecipe();
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateRecipe(recipe));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ParallaxArtworkRecipeManager> reloaded,
                       ParallaxArtworkRecipeManager::Create(path_.string()));
  ASSERT_OK(reloaded->LoadAllRecipes());
  ASSERT_OK_AND_ASSIGN(ParallaxArtworkRecipe * loaded, reloaded->GetRecipe(id));
  recipe.id = id;
  EXPECT_EQ(ParallaxArtworkRecipeToJson(*loaded), ParallaxArtworkRecipeToJson(recipe));
}

TEST_F(ParallaxArtworkRecipeManagerTest, DetachedStyleWritesAnExplicitNullReference) {
  ParallaxArtworkRecipe recipe = CompleteRecipe();
  recipe.terrain_recipe_id.reset();
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateRecipe(recipe));

  std::ifstream stream(path_ / "definitions/parallax_artwork_recipes" / (id + ".json"));
  nlohmann::json json;
  stream >> json;
  EXPECT_TRUE(json.at("terrain_recipe_id").is_null());
}

TEST_F(ParallaxArtworkRecipeManagerTest, CreateWithIdPreflightsMemoryAndDiskCollisions) {
  ParallaxArtworkRecipe recipe = CompleteRecipe();
  recipe.id = "prepared-recipe";

  ASSERT_OK(manager_->CreateRecipeWithId(recipe));
  EXPECT_EQ(manager_->CreateRecipeWithId(recipe).code(), absl::StatusCode::kAlreadyExists);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ParallaxArtworkRecipeManager> second,
                       ParallaxArtworkRecipeManager::Create(path_.string()));
  EXPECT_EQ(second->PreflightRecipeWithId(recipe).code(), absl::StatusCode::kAlreadyExists);
}

TEST_F(ParallaxArtworkRecipeManagerTest, LoadRejectsFilenameThatDoesNotMatchIdentity) {
  ParallaxArtworkRecipe recipe = CompleteRecipe();
  recipe.id = "inside-id";
  const std::filesystem::path directory = path_ / "definitions/parallax_artwork_recipes";
  std::ofstream(directory / "outside-id.json") << ParallaxArtworkRecipeToJson(recipe).dump(2);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ParallaxArtworkRecipeManager> reloaded,
                       ParallaxArtworkRecipeManager::Create(path_.string()));
  const absl::Status status = reloaded->LoadAllRecipes();

  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_NE(status.message().find("filename"), std::string_view::npos);
}

TEST_F(ParallaxArtworkRecipeManagerTest, LoadRejectsUnknownFieldsWithoutReplacingCurrentCatalog) {
  ParallaxArtworkRecipe current = CompleteRecipe("Current");
  current.id = "current";
  ASSERT_OK(manager_->CreateRecipeWithId(current));
  ParallaxArtworkRecipe broken = CompleteRecipe("Broken");
  broken.id = "broken";
  nlohmann::json json = ParallaxArtworkRecipeToJson(broken);
  json["pipeline"]["unknown"] = 1;
  const std::filesystem::path directory = path_ / "definitions/parallax_artwork_recipes";
  std::ofstream(directory / "broken.json") << json.dump(2);

  const absl::Status status = manager_->LoadAllRecipes();

  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  ASSERT_OK_AND_ASSIGN(ParallaxArtworkRecipe * still_loaded, manager_->GetRecipe("current"));
  EXPECT_EQ(still_loaded->name, "Current");
}

TEST_F(ParallaxArtworkRecipeManagerTest, SavingKeepsPointersStableAndListingIsSorted) {
  ParallaxArtworkRecipe zulu = CompleteRecipe("Zulu");
  zulu.texture_id = "texture-z";
  ASSERT_OK_AND_ASSIGN(const std::string zulu_id, manager_->CreateRecipe(zulu));
  ParallaxArtworkRecipe alpha = CompleteRecipe("Alpha");
  alpha.texture_id = "texture-a";
  ASSERT_OK(manager_->CreateRecipe(alpha).status());
  ASSERT_OK_AND_ASSIGN(ParallaxArtworkRecipe * held, manager_->GetRecipe(zulu_id));
  ParallaxArtworkRecipe edited = *held;
  edited.name = "Beta";
  ASSERT_OK(manager_->SaveRecipe(edited));

  ASSERT_OK_AND_ASSIGN(ParallaxArtworkRecipe * after, manager_->GetRecipe(zulu_id));
  EXPECT_EQ(after, held);
  EXPECT_EQ(held->name, "Beta");
  const std::vector<ParallaxArtworkRecipe> recipes = manager_->GetAllRecipes();
  ASSERT_EQ(recipes.size(), 2);
  EXPECT_EQ(recipes[0].name, "Alpha");
  EXPECT_EQ(recipes[1].name, "Beta");
}

TEST_F(ParallaxArtworkRecipeManagerTest, DeleteRemovesDefinitionAndCatalogEntry) {
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateRecipe(CompleteRecipe()));
  const std::filesystem::path definition =
      path_ / "definitions/parallax_artwork_recipes" / (id + ".json");

  ASSERT_OK(manager_->DeleteRecipe(id));

  EXPECT_FALSE(std::filesystem::exists(definition));
  EXPECT_EQ(manager_->GetRecipe(id).status().code(), absl::StatusCode::kNotFound);
}

}  // namespace
}  // namespace zebes
