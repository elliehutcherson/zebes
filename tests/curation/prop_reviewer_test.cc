#include "curation/prop_reviewer.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "api_mock.h"
#include "artwork/regenerate_prop_asset.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "generation/generated_asset_candidate.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "terrain/terrain_palette.h"
#include "terrain/terrain_style.h"

namespace zebes {
namespace {
using ::testing::_;
using ::testing::Return;

struct CandidateGraph {
  SourceArtwork source;
  RgbaImage source_pixels;
  PropRecipe recipe;
  Texture texture;
  RgbaImage texture_pixels;
  Sprite sprite;
  Blueprint blueprint;
  PropRecipe candidate;
};

absl::StatusOr<PropRecipe> RecipeFor(const RgbaImage& texture) {
  TerrainGenConfig terrain;
  terrain.tile_size = 16;
  ASSIGN_OR_RETURN(const ResolvedTerrainPalette palette, ResolveTerrainPalette(terrain));
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(texture));

  PropRecipe recipe;
  recipe.id = "recipe-id";
  recipe.name = "Crystal";
  recipe.source_artwork_id = "source-id";
  recipe.style = {.tile_size = 16, .pixel_block_size = 2, .palette = palette};
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
  recipe.pipeline.cleanup.contact_tolerance = 2;
  recipe.texture_id = "texture-id";
  recipe.sprite_id = "sprite-id";
  recipe.blueprint_id = "blueprint-id";
  recipe.expected_frame = {
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
  recipe.final_pixel_digest = digest;
  return recipe;
}

RgbaImage SubjectPixels() {
  RgbaImage pixels{
      .width = 48,
      .height = 64,
      .pixels = std::vector<uint8_t>(48 * 64 * 4, 0),
  };
  for (int y = 8; y < 63; ++y) {
    for (int x = 12; x < 36; ++x) {
      const size_t offset = (static_cast<size_t>(y) * pixels.width + x) * 4;
      pixels.pixels[offset] = 120;
      pixels.pixels[offset + 1] = 160;
      pixels.pixels[offset + 2] = 220;
      pixels.pixels[offset + 3] = 255;
    }
  }
  return pixels;
}

absl::StatusOr<CandidateGraph> MakeCandidateGraph() {
  RgbaImage pixels = SubjectPixels();
  ASSIGN_OR_RETURN(PropRecipe recipe, RecipeFor(pixels));
  ASSIGN_OR_RETURN(const std::string source_digest, RgbaImageDigest(pixels));
  SourceArtwork source{
      .id = recipe.source_artwork_id,
      .name = "Crystal source",
      .source_path = "source_artwork/source-id.png",
      .provenance =
          ImportedArtworkProvenance{
              .original_filename = "crystal.png",
              .imported_at_utc = "2026-08-25T12:00:00Z",
          },
      .width = pixels.width,
      .height = pixels.height,
      .content_digest = source_digest,
  };
  Texture texture{
      .id = recipe.texture_id,
      .name = recipe.name,
      .path = "textures/props/texture-id.png",
  };
  Sprite sprite{
      .id = recipe.sprite_id,
      .name = recipe.name,
      .texture_id = texture.id,
      .frames = {recipe.expected_frame},
  };
  Blueprint blueprint{
      .id = recipe.blueprint_id,
      .name = recipe.name,
      .states = {{
          .name = "Default",
          .sprite_id = sprite.id,
          .placement_mode = BlueprintPlacementMode::kGrounded,
      }},
  };
  ASSIGN_OR_RETURN(PreparedPropRegeneration prepared,
                   PreparePropRegeneration(source, pixels, recipe, texture, pixels, sprite,
                                           PropRegenerationSettings{
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
      .sprite = std::move(sprite),
      .blueprint = std::move(blueprint),
      .candidate = std::move(prepared.updated_recipe),
  };
}

void ExpectCandidateInputs(MockApi& api, CandidateGraph& graph, bool include_blueprint) {
  EXPECT_CALL(api, GetPropRecipe(graph.recipe.id)).WillOnce(Return(&graph.recipe));
  EXPECT_CALL(api, GetSourceArtwork(graph.source.id)).WillOnce(Return(&graph.source));
  EXPECT_CALL(api, GetTexture(graph.texture.id)).WillOnce(Return(&graph.texture));
  EXPECT_CALL(api, GetSprite(graph.sprite.id)).WillOnce(Return(&graph.sprite));
  EXPECT_CALL(api, ReadSourceArtworkPixels(graph.source.id)).WillOnce(Return(graph.source_pixels));
  EXPECT_CALL(api, ReadTexturePixels(graph.texture.id)).WillOnce(Return(graph.texture_pixels));
  if (include_blueprint) {
    EXPECT_CALL(api, GetBlueprint(graph.blueprint.id)).WillOnce(Return(&graph.blueprint));
  }
}

TEST(PropReviewerTest, ResolvesTheManagedBundleAndRendersNativeDetailAndPlacementViews) {
  RgbaImage pixels = SubjectPixels();
  ASSERT_OK_AND_ASSIGN(PropRecipe recipe, RecipeFor(pixels));
  Texture texture{.id = recipe.texture_id, .name = recipe.name, .path = "texture.png"};
  Sprite sprite{
      .id = recipe.sprite_id,
      .name = recipe.name,
      .texture_id = texture.id,
      .frames = {recipe.expected_frame},
  };
  Blueprint blueprint{
      .id = recipe.blueprint_id,
      .name = recipe.name,
      .states = {{
          .name = "Default",
          .sprite_id = sprite.id,
          .placement_mode = BlueprintPlacementMode::kGrounded,
      }},
  };
  MockApi api;
  EXPECT_CALL(api, GetPropRecipe(recipe.id)).WillOnce(Return(&recipe));
  EXPECT_CALL(api, GetTexture(texture.id)).WillOnce(Return(&texture));
  EXPECT_CALL(api, GetSprite(sprite.id)).WillOnce(Return(&sprite));
  EXPECT_CALL(api, GetBlueprint(blueprint.id)).WillOnce(Return(&blueprint));
  EXPECT_CALL(api, ReadTexturePixels(texture.id)).WillOnce(Return(pixels));

  PropReviewer reviewer;
  ASSERT_OK_AND_ASSIGN(CurationReview review, reviewer.Review(api, {.asset_id = recipe.id}));

  EXPECT_EQ(review.kind, "prop");
  ASSERT_EQ(review.artifacts.size(), 3);
  EXPECT_EQ(review.artifacts.front().image.width, pixels.width);
  EXPECT_EQ(review.artifacts.front().image.height, pixels.height);
  EXPECT_EQ(review.artifacts.front().image.pixels, pixels.pixels);
  EXPECT_EQ(review.metadata.at("placement_mode"), "grounded");
  EXPECT_EQ(review.metadata.at("recipe").at("id"), recipe.id);
  ASSERT_EQ(review.findings.size(), 1);
  EXPECT_EQ(review.findings.front().code, "transparent-edge-clearance");
}

TEST(PropReviewerTest, ReviewsAndCommitsAnExactDeterministicRecipeCandidate) {
  ASSERT_OK_AND_ASSIGN(CandidateGraph graph, MakeCandidateGraph());
  const nlohmann::json candidate = PropRecipeToJson(graph.candidate);
  PropReviewer reviewer;

  MockApi review_api;
  ExpectCandidateInputs(review_api, graph, true);
  ASSERT_OK_AND_ASSIGN(
      CurationReview review,
      reviewer.ReviewCandidate(review_api, {.asset_id = graph.recipe.id}, candidate));
  EXPECT_TRUE(review.metadata.at("candidate"));
  EXPECT_TRUE(review.metadata.at("candidate_matches_deterministic_output"));

  MockApi commit_api;
  ExpectCandidateInputs(commit_api, graph, false);
  EXPECT_CALL(commit_api, RegenerateGeneratedProp)
      .WillOnce([](const PreparedPropRegeneration& prepared) {
        return ValidatePreparedPropRegeneration(prepared);
      });
  EXPECT_OK(reviewer.CommitCandidate(commit_api, {.asset_id = graph.recipe.id}, candidate));
}

TEST(PropReviewerTest, ReviewsAndCommitsGeneratedPixelsAsANewAsset) {
  ASSERT_OK_AND_ASSIGN(CandidateGraph graph, MakeCandidateGraph());
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      std::filesystem::path("zebes-generated-prop-candidate-" + GenerateGuid());
  ASSERT_TRUE(std::filesystem::create_directories(root));
  ASSERT_OK(WritePng((root / "processed-source.png").string(), graph.source_pixels.width,
                     graph.source_pixels.height, graph.source_pixels.pixels));
  ASSERT_OK_AND_ASSIGN(const std::string digest, RgbaImageDigest(graph.source_pixels));
  GeneratedPropCreationCandidate candidate{
      .asset_id = "new-recipe-id",
      .name = "Generated Crystal",
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
                      .submitted_prompt = "a generated crystal",
                      .generated_at_utc = "2026-08-25T12:00:00Z",
                  },
          },
      .template_recipe = graph.recipe,
      .ids =
          {
              .texture_id = "new-texture-id",
              .sprite_id = "new-sprite-id",
              .blueprint_id = "new-blueprint-id",
              .recipe_id = "new-recipe-id",
          },
  };
  const nlohmann::json candidate_json = GeneratedPropCreationCandidateToJson(candidate);
  const CurationReviewRequest request{
      .asset_id = candidate.asset_id,
      .candidate_root = root.string(),
  };
  PropReviewer reviewer;
  MockApi review_api;
  ASSERT_OK_AND_ASSIGN(CurationReview review,
                       reviewer.ReviewCandidate(review_api, request, candidate_json));
  EXPECT_EQ(review.metadata.at("candidate_operation"), "create");
  EXPECT_TRUE(review.metadata.at("candidate_matches_deterministic_output"));

  SourceArtwork retained{
      .id = "retained-source-id",
      .name = "Generated Crystal source",
      .source_path = "source_art/retained-source-id.png",
      .provenance = candidate.source.provenance,
      .width = graph.source_pixels.width,
      .height = graph.source_pixels.height,
      .content_digest = digest,
  };
  MockApi commit_api;
  EXPECT_CALL(commit_api, CreateSourceArtwork(_, _, _)).WillOnce(Return(retained.id));
  EXPECT_CALL(commit_api, GetSourceArtwork(retained.id)).WillOnce(Return(&retained));
  EXPECT_CALL(commit_api, CreateGeneratedProp)
      .WillOnce([&candidate](const PreparedPropAsset& prepared) -> absl::StatusOr<std::string> {
        const absl::Status valid = ValidatePreparedPropAsset(prepared);
        if (!valid.ok()) return valid;
        return candidate.asset_id;
      });
  EXPECT_CALL(commit_api, DeleteSourceArtwork).Times(0);
  EXPECT_OK(reviewer.CommitCandidate(commit_api, request, candidate_json));

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

TEST(PropReviewerTest, DeletesRetainedSourceWhenNewAssetCreationFails) {
  ASSERT_OK_AND_ASSIGN(CandidateGraph graph, MakeCandidateGraph());
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      std::filesystem::path("zebes-generated-prop-failure-" + GenerateGuid());
  ASSERT_TRUE(std::filesystem::create_directories(root));
  ASSERT_OK(WritePng((root / "processed-source.png").string(), graph.source_pixels.width,
                     graph.source_pixels.height, graph.source_pixels.pixels));
  ASSERT_OK_AND_ASSIGN(const std::string digest, RgbaImageDigest(graph.source_pixels));
  GeneratedPropCreationCandidate candidate{
      .asset_id = "new-recipe-id",
      .name = "Generated Crystal",
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
                      .submitted_prompt = "a generated crystal",
                      .generated_at_utc = "2026-08-25T12:00:00Z",
                  },
          },
      .template_recipe = graph.recipe,
      .ids =
          {
              .texture_id = "new-texture-id",
              .sprite_id = "new-sprite-id",
              .blueprint_id = "new-blueprint-id",
              .recipe_id = "new-recipe-id",
          },
  };
  SourceArtwork retained{
      .id = "retained-source-id",
      .name = "Generated Crystal source",
      .source_path = "source_art/retained-source-id.png",
      .provenance = candidate.source.provenance,
      .width = graph.source_pixels.width,
      .height = graph.source_pixels.height,
      .content_digest = digest,
  };
  MockApi api;
  EXPECT_CALL(api, CreateSourceArtwork(_, _, _)).WillOnce(Return(retained.id));
  EXPECT_CALL(api, GetSourceArtwork(retained.id)).WillOnce(Return(&retained));
  EXPECT_CALL(api, CreateGeneratedProp)
      .WillOnce(Return(absl::InternalError("injected bundle failure")));
  EXPECT_CALL(api, DeleteSourceArtwork(retained.id)).WillOnce(Return(absl::OkStatus()));
  PropReviewer reviewer;

  EXPECT_TRUE(absl::IsInternal(reviewer.CommitCandidate(
      api, {.asset_id = candidate.asset_id, .candidate_root = root.string()},
      GeneratedPropCreationCandidateToJson(candidate))));

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

TEST(PropReviewerTest, PublishesReviewEvidenceButRefusesACandidateDigestMismatch) {
  ASSERT_OK_AND_ASSIGN(CandidateGraph graph, MakeCandidateGraph());
  PropRecipe corrupt = graph.candidate;
  corrupt.final_pixel_digest = std::string(64, '0');
  const nlohmann::json candidate = PropRecipeToJson(corrupt);
  PropReviewer reviewer;

  MockApi review_api;
  ExpectCandidateInputs(review_api, graph, true);
  ASSERT_OK_AND_ASSIGN(
      CurationReview review,
      reviewer.ReviewCandidate(review_api, {.asset_id = graph.recipe.id}, candidate));
  EXPECT_FALSE(review.metadata.at("candidate_matches_deterministic_output"));
  EXPECT_EQ(review.findings.front().code, "candidate-recipe-mismatch");

  MockApi commit_api;
  ExpectCandidateInputs(commit_api, graph, false);
  EXPECT_TRUE(absl::IsFailedPrecondition(
      reviewer.CommitCandidate(commit_api, {.asset_id = graph.recipe.id}, candidate)));
}

TEST(PropReviewerTest, RefusesMismatchedIdsInvalidSchemasAndTransactionFailures) {
  ASSERT_OK_AND_ASSIGN(CandidateGraph graph, MakeCandidateGraph());
  PropReviewer reviewer;
  nlohmann::json candidate = PropRecipeToJson(graph.candidate);

  MockApi mismatched_api;
  EXPECT_TRUE(absl::IsInvalidArgument(
      reviewer.ReviewCandidate(mismatched_api, {.asset_id = "different-id"}, candidate).status()));

  nlohmann::json invalid = candidate;
  invalid["schema_version"] = -1;
  MockApi invalid_api;
  EXPECT_TRUE(absl::IsFailedPrecondition(
      reviewer.ReviewCandidate(invalid_api, {.asset_id = graph.recipe.id}, invalid).status()));

  MockApi commit_api;
  ExpectCandidateInputs(commit_api, graph, false);
  EXPECT_CALL(commit_api, RegenerateGeneratedProp)
      .WillOnce(Return(absl::InternalError("injected transaction failure")));
  EXPECT_TRUE(absl::IsInternal(
      reviewer.CommitCandidate(commit_api, {.asset_id = graph.recipe.id}, candidate)));
}

}  // namespace
}  // namespace zebes
