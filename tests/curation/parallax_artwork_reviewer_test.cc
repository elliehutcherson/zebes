#include "curation/parallax_artwork_reviewer.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "api_mock.h"
#include "artwork/regenerate_parallax_artwork_asset.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "generation/generated_asset_candidate.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {
using ::testing::_;
using ::testing::Return;

struct CandidateGraph {
  SourceArtwork source;
  RgbaImage source_pixels;
  ParallaxArtworkRecipe recipe;
  Texture texture;
  RgbaImage texture_pixels;
  ParallaxArtworkRecipe candidate;
};

RgbaImage PlatePixels() {
  RgbaImage image{
      .width = 8,
      .height = 8,
      .pixels = std::vector<uint8_t>(8 * 8 * 4, 255),
  };
  for (size_t offset = 0; offset < image.pixels.size(); offset += 4) {
    image.pixels[offset + 0] = 18;
    image.pixels[offset + 1] = 36;
    image.pixels[offset + 2] = 72;
  }
  return image;
}

absl::StatusOr<CandidateGraph> MakeCandidateGraph() {
  RgbaImage pixels = PlatePixels();
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(pixels));
  SourceArtwork source{
      .id = "source-id",
      .name = "Cave plate source",
      .source_path = "source_artwork/source-id.png",
      .provenance =
          ImportedArtworkProvenance{
              .original_filename = "plate.png",
              .imported_at_utc = "2026-08-25T12:00:00Z",
          },
      .width = pixels.width,
      .height = pixels.height,
      .content_digest = digest,
  };
  ParallaxArtworkRecipe recipe{
      .id = "recipe-id",
      .name = "Cave Plate",
      .source_artwork_id = source.id,
      .style =
          {
              .pixel_block_size = 1,
              .quantize_to_palette = false,
          },
      .pipeline =
          {
              .target_width = pixels.width,
              .target_height = pixels.height,
              .frame_policy = ParallaxArtworkFramePolicy::kCropToFill,
              .alpha_role = ParallaxArtworkAlphaRole::kOpaquePlate,
              .review_repeat_x = true,
          },
      .texture_id = "texture-id",
      .expected_width = pixels.width,
      .expected_height = pixels.height,
      .final_pixel_digest = digest,
  };
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(recipe));
  Texture texture{
      .id = recipe.texture_id,
      .name = recipe.name,
      .path = "textures/parallax_artwork/texture-id.png",
  };
  ASSIGN_OR_RETURN(
      PreparedParallaxArtworkRegeneration prepared,
      PrepareParallaxArtworkRegeneration(source, pixels, recipe, texture, pixels,
                                         ParallaxArtworkRegenerationSettings{
                                             .terrain_recipe_id = recipe.terrain_recipe_id,
                                             .style = recipe.style,
                                             .pipeline = recipe.pipeline,
                                         }));
  return CandidateGraph{
      .source = std::move(source),
      .source_pixels = pixels,
      .recipe = std::move(recipe),
      .texture = std::move(texture),
      .texture_pixels = pixels,
      .candidate = std::move(prepared.updated_recipe),
  };
}

void ExpectCandidateInputs(MockApi& api, CandidateGraph& graph) {
  EXPECT_CALL(api, GetParallaxArtworkRecipe(graph.recipe.id)).WillOnce(Return(&graph.recipe));
  EXPECT_CALL(api, GetSourceArtwork(graph.source.id)).WillOnce(Return(&graph.source));
  EXPECT_CALL(api, GetTexture(graph.texture.id)).WillOnce(Return(&graph.texture));
  EXPECT_CALL(api, ReadSourceArtworkPixels(graph.source.id)).WillOnce(Return(graph.source_pixels));
  EXPECT_CALL(api, ReadTexturePixels(graph.texture.id)).WillOnce(Return(graph.texture_pixels));
}

TEST(ParallaxArtworkReviewerTest, ReviewsPersistedNativeDetailAndRepeatEvidence) {
  ASSERT_OK_AND_ASSIGN(CandidateGraph graph, MakeCandidateGraph());
  MockApi api;
  EXPECT_CALL(api, GetParallaxArtworkRecipe(graph.recipe.id)).WillOnce(Return(&graph.recipe));
  EXPECT_CALL(api, GetTexture(graph.texture.id)).WillOnce(Return(&graph.texture));
  EXPECT_CALL(api, ReadTexturePixels(graph.texture.id)).WillOnce(Return(graph.texture_pixels));

  ParallaxArtworkReviewer reviewer;
  ASSERT_OK_AND_ASSIGN(CurationReview review, reviewer.Review(api, {.asset_id = graph.recipe.id}));

  EXPECT_EQ(review.kind, "parallax-artwork");
  ASSERT_EQ(review.artifacts.size(), 3);
  EXPECT_EQ(review.artifacts.at(2).id, "repeat-x");
  EXPECT_EQ(review.metadata.at("recipe").at("id"), graph.recipe.id);
}

TEST(ParallaxArtworkReviewerTest, ReviewsAndCommitsAnExactRecipeCandidate) {
  ASSERT_OK_AND_ASSIGN(CandidateGraph graph, MakeCandidateGraph());
  const nlohmann::json candidate = ParallaxArtworkRecipeToJson(graph.candidate);
  ParallaxArtworkReviewer reviewer;

  MockApi review_api;
  ExpectCandidateInputs(review_api, graph);
  ASSERT_OK_AND_ASSIGN(
      CurationReview review,
      reviewer.ReviewCandidate(review_api, {.asset_id = graph.recipe.id}, candidate));
  EXPECT_TRUE(review.metadata.at("candidate_matches_deterministic_output"));

  MockApi commit_api;
  ExpectCandidateInputs(commit_api, graph);
  EXPECT_CALL(commit_api, RegenerateGeneratedParallaxArtwork)
      .WillOnce([](const PreparedParallaxArtworkRegeneration& prepared) {
        return ValidatePreparedParallaxArtworkRegeneration(prepared);
      });
  EXPECT_OK(reviewer.CommitCandidate(commit_api, {.asset_id = graph.recipe.id}, candidate));
}

TEST(ParallaxArtworkReviewerTest, ReviewsAndCommitsGeneratedPixelsAsANewAsset) {
  ASSERT_OK_AND_ASSIGN(CandidateGraph graph, MakeCandidateGraph());
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      std::filesystem::path("zebes-generated-parallax-candidate-" + GenerateGuid());
  ASSERT_TRUE(std::filesystem::create_directories(root));
  ASSERT_OK(WritePng((root / "processed-source.png").string(), graph.source_pixels.width,
                     graph.source_pixels.height, graph.source_pixels.pixels));
  ASSERT_OK_AND_ASSIGN(const std::string digest, RgbaImageDigest(graph.source_pixels));
  GeneratedParallaxArtworkCreationCandidate candidate{
      .asset_id = "new-recipe-id",
      .name = "Generated Cave Plate",
      .source =
          {
              .relative_path = "processed-source.png",
              .width = graph.source_pixels.width,
              .height = graph.source_pixels.height,
              .content_digest = digest,
              .provenance =
                  {
                      .provider = "fake",
                      .model = "zebes-fake-v1",
                      .submitted_prompt = "a generated cave plate",
                      .generated_at_utc = "2026-08-25T12:00:00Z",
                  },
          },
      .template_recipe = graph.recipe,
      .ids = {.texture_id = "new-texture-id", .recipe_id = "new-recipe-id"},
  };
  const nlohmann::json candidate_json = GeneratedParallaxArtworkCreationCandidateToJson(candidate);
  const CurationReviewRequest request{
      .asset_id = candidate.asset_id,
      .candidate_root = root.string(),
  };
  ParallaxArtworkReviewer reviewer;
  MockApi review_api;
  ASSERT_OK_AND_ASSIGN(CurationReview review,
                       reviewer.ReviewCandidate(review_api, request, candidate_json));
  EXPECT_EQ(review.metadata.at("candidate_operation"), "create");
  EXPECT_TRUE(review.metadata.at("candidate_matches_deterministic_output"));

  SourceArtwork retained{
      .id = "retained-source-id",
      .name = "Generated Cave Plate source",
      .source_path = "source_art/retained-source-id.png",
      .provenance = candidate.source.provenance,
      .width = graph.source_pixels.width,
      .height = graph.source_pixels.height,
      .content_digest = digest,
  };
  MockApi commit_api;
  EXPECT_CALL(commit_api, CreateSourceArtwork(_, _, _)).WillOnce(Return(retained.id));
  EXPECT_CALL(commit_api, GetSourceArtwork(retained.id)).WillOnce(Return(&retained));
  EXPECT_CALL(commit_api, CreateGeneratedParallaxArtwork)
      .WillOnce([&candidate](
                    const PreparedParallaxArtworkAsset& prepared) -> absl::StatusOr<std::string> {
        const absl::Status valid = ValidatePreparedParallaxArtworkAsset(prepared);
        if (!valid.ok()) return valid;
        return candidate.asset_id;
      });
  EXPECT_CALL(commit_api, DeleteSourceArtwork).Times(0);
  EXPECT_OK(reviewer.CommitCandidate(commit_api, request, candidate_json));

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

TEST(ParallaxArtworkReviewerTest, RefusesIdSchemaDigestAndTransactionFailures) {
  ASSERT_OK_AND_ASSIGN(CandidateGraph graph, MakeCandidateGraph());
  ParallaxArtworkReviewer reviewer;
  const nlohmann::json candidate = ParallaxArtworkRecipeToJson(graph.candidate);

  MockApi mismatched_api;
  EXPECT_TRUE(absl::IsInvalidArgument(
      reviewer.ReviewCandidate(mismatched_api, {.asset_id = "different-id"}, candidate).status()));

  nlohmann::json invalid = candidate;
  invalid["schema_version"] = -1;
  MockApi invalid_api;
  EXPECT_TRUE(absl::IsFailedPrecondition(
      reviewer.ReviewCandidate(invalid_api, {.asset_id = graph.recipe.id}, invalid).status()));

  ParallaxArtworkRecipe corrupt = graph.candidate;
  corrupt.final_pixel_digest = std::string(64, '0');
  const nlohmann::json corrupt_json = ParallaxArtworkRecipeToJson(corrupt);
  MockApi review_api;
  ExpectCandidateInputs(review_api, graph);
  ASSERT_OK_AND_ASSIGN(
      CurationReview review,
      reviewer.ReviewCandidate(review_api, {.asset_id = graph.recipe.id}, corrupt_json));
  EXPECT_FALSE(review.metadata.at("candidate_matches_deterministic_output"));
  MockApi digest_api;
  ExpectCandidateInputs(digest_api, graph);
  EXPECT_TRUE(absl::IsFailedPrecondition(
      reviewer.CommitCandidate(digest_api, {.asset_id = graph.recipe.id}, corrupt_json)));

  MockApi commit_api;
  ExpectCandidateInputs(commit_api, graph);
  EXPECT_CALL(commit_api, RegenerateGeneratedParallaxArtwork)
      .WillOnce(Return(absl::InternalError("injected transaction failure")));
  EXPECT_TRUE(absl::IsInternal(
      reviewer.CommitCandidate(commit_api, {.asset_id = graph.recipe.id}, candidate)));
}

}  // namespace
}  // namespace zebes
