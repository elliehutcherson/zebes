#include "artwork/parallax_artwork_recipe.h"

#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"
#include "tests/macros.h"

namespace zebes {
namespace {

ParallaxArtworkRecipe ValidRecipe() {
  return {
      .id = "recipe-id",
      .name = "Far Cave Plate",
      .source_artwork_id = "source-id",
      .terrain_recipe_id = "terrain-id",
      .style = {.pixel_block_size = 2,
                .quantize_to_palette = true,
                .palette = {RgbaColor{10, 16, 59, 255}, RgbaColor{81, 80, 126, 255}}},
      .pipeline = {.target_width = 8, .target_height = 4},
      .texture_id = "texture-id",
      .expected_width = 8,
      .expected_height = 4,
      .final_pixel_digest = std::string(64, 'a'),
  };
}

TEST(ParallaxArtworkRecipeTest, RoundTripsEveryAuthoredField) {
  const ParallaxArtworkRecipe recipe = ValidRecipe();

  ASSERT_OK_AND_ASSIGN(const ParallaxArtworkRecipe loaded,
                       ParallaxArtworkRecipeFromJson(ParallaxArtworkRecipeToJson(recipe)));

  EXPECT_EQ(ParallaxArtworkRecipeToJson(loaded), ParallaxArtworkRecipeToJson(recipe));
}

TEST(ParallaxArtworkRecipeTest, RejectsUnknownRootAndNestedFields) {
  nlohmann::json root = ParallaxArtworkRecipeToJson(ValidRecipe());
  root["surprise"] = true;
  nlohmann::json nested = ParallaxArtworkRecipeToJson(ValidRecipe());
  nested["pipeline"]["surprise"] = true;

  const absl::Status root_status = ParallaxArtworkRecipeFromJson(root).status();
  const absl::Status nested_status = ParallaxArtworkRecipeFromJson(nested).status();

  EXPECT_EQ(root_status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(root_status.message().find("unknown field"), std::string_view::npos);
  EXPECT_EQ(nested_status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(nested_status.message().find("unknown field"), std::string_view::npos);
}

TEST(ParallaxArtworkRecipeTest, RejectsMissingOptionalRelationshipField) {
  nlohmann::json json = ParallaxArtworkRecipeToJson(ValidRecipe());
  json.erase("terrain_recipe_id");

  const absl::Status status = ParallaxArtworkRecipeFromJson(json).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("terrain_recipe_id"), std::string_view::npos);
}

TEST(ParallaxArtworkRecipeTest, RejectsUnknownSchemaAndPipelineVersions) {
  nlohmann::json schema = ParallaxArtworkRecipeToJson(ValidRecipe());
  schema["schema_version"] = 2;
  ParallaxArtworkRecipe pipeline = ValidRecipe();
  pipeline.pipeline_version = 2;

  EXPECT_EQ(ParallaxArtworkRecipeFromJson(schema).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(ValidateParallaxArtworkRecipe(pipeline).code(), absl::StatusCode::kFailedPrecondition);
}

TEST(ParallaxArtworkRecipeTest, RejectsOutputDimensionsThatDisagreeWithPipeline) {
  ParallaxArtworkRecipe recipe = ValidRecipe();
  recipe.expected_width = 7;

  const absl::Status status = ValidateParallaxArtworkRecipe(recipe);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("dimensions"), std::string_view::npos);
}

TEST(ParallaxArtworkRecipeTest, RejectsInvalidDigestAndPaletteSnapshot) {
  ParallaxArtworkRecipe digest = ValidRecipe();
  digest.final_pixel_digest = std::string(64, 'A');
  ParallaxArtworkRecipe palette = ValidRecipe();
  palette.style.palette.clear();
  palette.style.quantize_to_palette = false;

  EXPECT_EQ(ValidateParallaxArtworkRecipe(digest).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ValidateParallaxArtworkRecipe(palette).code(), absl::StatusCode::kInvalidArgument);
}

TEST(ParallaxArtworkRecipeTest, PermitsUnattachedPalettePreservingRecipe) {
  ParallaxArtworkRecipe recipe = ValidRecipe();
  recipe.terrain_recipe_id.reset();
  recipe.style.quantize_to_palette = false;
  recipe.style.palette.clear();

  EXPECT_OK(ValidateParallaxArtworkRecipe(recipe));
}

}  // namespace
}  // namespace zebes
