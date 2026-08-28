#include "generation/headless_asset_generation.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "api_mock.h"
#include "artwork/parallax_artwork_recipe.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "generation/fake_image_generation_client.h"
#include "generation/generated_asset_candidate.h"
#include "generation/image_generation_service.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

using ::testing::Return;

class HeadlessAssetGenerationTest : public ::testing::Test {
 protected:
  HeadlessAssetGenerationTest()
      : root_(std::filesystem::temp_directory_path() /
              std::filesystem::path("zebes-headless-generation-" + GenerateGuid())) {}

  ~HeadlessAssetGenerationTest() override {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  std::filesystem::path root_;
};

absl::StatusOr<ParallaxArtworkRecipe> TemplateRecipe() {
  RgbaImage pixels{
      .width = 8,
      .height = 8,
      .pixels = std::vector<uint8_t>(8 * 8 * 4, 255),
  };
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(pixels));
  ParallaxArtworkRecipe recipe{
      .id = "template-recipe",
      .name = "Template",
      .source_artwork_id = "template-source",
      .style = {.pixel_block_size = 1, .quantize_to_palette = false},
      .pipeline =
          {
              .target_width = 16,
              .target_height = 9,
              .frame_policy = ParallaxArtworkFramePolicy::kCropToFill,
              .alpha_role = ParallaxArtworkAlphaRole::kOpaquePlate,
              .review_repeat_x = true,
          },
      .texture_id = "template-texture",
      .expected_width = 16,
      .expected_height = 9,
      .final_pixel_digest = digest,
  };
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(recipe));
  return recipe;
}

TEST_F(HeadlessAssetGenerationTest, FakeProviderPublishesACompleteStrictCandidateBundle) {
  ASSERT_OK_AND_ASSIGN(ParallaxArtworkRecipe recipe, TemplateRecipe());
  MockApi api;
  EXPECT_CALL(api, GetParallaxArtworkRecipe(recipe.id)).WillOnce(Return(&recipe));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(CreateFakeImageGenerationClient()));
  const std::filesystem::path output = root_ / "candidate";

  ASSERT_OK_AND_ASSIGN(HeadlessAssetGenerationResult result,
                       GenerateAssetCandidateBundle(api, *service,
                                                    {
                                                        .kind = "parallax-artwork",
                                                        .template_recipe_id = recipe.id,
                                                        .name = "Fake Cave Background",
                                                        .prompt = "a deep violet cave",
                                                        .output_path = output.string(),
                                                    }));

  EXPECT_TRUE(std::filesystem::is_regular_file(result.manifest_path));
  EXPECT_TRUE(std::filesystem::is_regular_file(result.candidate_path));
  EXPECT_TRUE(std::filesystem::is_regular_file(output / "generated-source.png"));
  EXPECT_TRUE(std::filesystem::is_regular_file(output / "processed-source.png"));
  std::ifstream candidate_stream(result.candidate_path);
  nlohmann::json candidate_json;
  candidate_stream >> candidate_json;
  ASSERT_OK_AND_ASSIGN(GeneratedParallaxArtworkCreationCandidate candidate,
                       GeneratedParallaxArtworkCreationCandidateFromJson(candidate_json));
  EXPECT_EQ(candidate.asset_id, result.asset_id);
  EXPECT_EQ(candidate.ids.recipe_id, result.asset_id);
  EXPECT_EQ(candidate.template_recipe.id, recipe.id);
  EXPECT_EQ(candidate.source.provenance.provider, "fake");
  ASSERT_OK(ReadGeneratedAssetSourceCandidate(output, candidate.source).status());

  const absl::Status status = GenerateAssetCandidateBundle(api, *service,
                                                           {
                                                               .kind = "parallax-artwork",
                                                               .template_recipe_id = recipe.id,
                                                               .name = "Fake Cave Background",
                                                               .prompt = "a deep violet cave",
                                                               .output_path = output.string(),
                                                           })
                                  .status();

  EXPECT_TRUE(absl::IsAlreadyExists(status));
}

TEST_F(HeadlessAssetGenerationTest, FakeProviderPublishesAStaleProtectedRedrawBundle) {
  ASSERT_OK_AND_ASSIGN(ParallaxArtworkRecipe recipe, TemplateRecipe());
  RgbaImage source_pixels{
      .width = 8,
      .height = 8,
      .pixels = std::vector<uint8_t>(8 * 8 * 4, 255),
  };
  ASSERT_OK_AND_ASSIGN(const std::string digest, RgbaImageDigest(source_pixels));
  recipe.final_pixel_digest = digest;
  SourceArtwork source{
      .id = recipe.source_artwork_id,
      .name = "Template source",
      .source_path = "source_artwork/template-source.png",
      .provenance =
          ImportedArtworkProvenance{
              .original_filename = "template.png",
              .imported_at_utc = "2026-08-27T12:00:00Z",
          },
      .width = source_pixels.width,
      .height = source_pixels.height,
      .content_digest = digest,
  };
  MockApi api;
  EXPECT_CALL(api, GetParallaxArtworkRecipe(recipe.id)).WillOnce(Return(&recipe));
  EXPECT_CALL(api, GetSourceArtwork(source.id)).WillOnce(Return(&source));
  EXPECT_CALL(api, ReadSourceArtworkPixels(source.id)).WillOnce(Return(source_pixels));
  EXPECT_CALL(api, ReadTexturePixels(recipe.texture_id)).WillOnce(Return(source_pixels));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(CreateFakeImageGenerationClient()));
  const std::filesystem::path output = root_ / "redraw";

  ASSERT_OK_AND_ASSIGN(HeadlessAssetGenerationResult result,
                       GenerateAssetRedrawCandidateBundle(api, *service,
                                                          {
                                                              .asset_id = recipe.id,
                                                              .prompt = "soften both side edges",
                                                              .output_path = output.string(),
                                                          }));

  EXPECT_EQ(result.asset_id, recipe.id);
  EXPECT_TRUE(std::filesystem::is_regular_file(output / "reference-source.png"));
  EXPECT_TRUE(std::filesystem::is_regular_file(output / "generated-source.png"));
  EXPECT_TRUE(std::filesystem::is_regular_file(output / "processed-source.png"));
  std::ifstream candidate_stream(result.candidate_path);
  nlohmann::json candidate_json;
  candidate_stream >> candidate_json;
  ASSERT_OK_AND_ASSIGN(GeneratedParallaxArtworkRedrawCandidate candidate,
                       GeneratedParallaxArtworkRedrawCandidateFromJson(candidate_json));
  EXPECT_EQ(candidate.asset_id, recipe.id);
  EXPECT_EQ(candidate.expected_source_digest, digest);
  EXPECT_EQ(candidate.expected_final_pixel_digest, digest);
  EXPECT_EQ(candidate.source.provenance.provider, "fake");
  ASSERT_OK(ReadGeneratedAssetSourceCandidate(output, candidate.source).status());
}

}  // namespace
}  // namespace zebes
