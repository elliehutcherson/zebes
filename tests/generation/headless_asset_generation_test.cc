#include "generation/headless_asset_generation.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "api_mock.h"
#include "artwork/parallax_artwork_recipe.h"
#include "artwork/prop_recipe.h"
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

class InstructionEchoOperation final : public ImageGenerationOperation {
 public:
  explicit InstructionEchoOperation(ImageGenerationSpec spec) : spec_(std::move(spec)) {}

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll() override {
    if (finished_) return std::nullopt;
    finished_ = true;
    return std::optional<ImageGenerationResult>(ImageGenerationResult{
        .provider = "instruction-echo",
        .model = "instruction-echo-v1",
        .submitted_prompt = spec_.prompt,
        .candidates = {{
            .image = {.width = 16, .height = 9, .pixels = std::vector<uint8_t>(16 * 9 * 4, 255)},
            .revised_prompt = spec_.instructions,
        }},
    });
  }

  void Cancel() noexcept override { finished_ = true; }

 private:
  ImageGenerationSpec spec_;
  bool finished_ = false;
};

class InstructionEchoClient final : public ImageGenerationClient {
 public:
  ImageGenerationCapabilities Capabilities() const override {
    return {.maximum_candidates = 1, .supports_transparency = false};
  }

 protected:
  absl::StatusOr<ImageGenerationRequest> StartValidated(ImageGenerationSpec spec) override {
    return ImageGenerationRequest::Create(
        std::make_unique<InstructionEchoOperation>(std::move(spec)));
  }
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

absl::StatusOr<PropRecipe> PropTemplateRecipe() {
  ResolvedTerrainPalette palette;
  palette.colors.front() = {.r = 0, .g = 0, .b = 0, .a = 0};
  for (size_t index = 1; index < palette.colors.size(); ++index) {
    const uint8_t channel = static_cast<uint8_t>(index * 7);
    palette.colors[index] = {.r = channel, .g = channel, .b = channel, .a = 255};
  }
  PropRecipe recipe{
      .id = "prop-template-recipe",
      .name = "Prop Template",
      .source_artwork_id = "prop-template-source",
      .style = {.tile_size = 32, .pixel_block_size = 1, .palette = palette},
      .texture_id = "prop-template-texture",
      .sprite_id = "prop-template-sprite",
      .blueprint_id = "prop-template-blueprint",
      .expected_frame =
          {
              .index = 0,
              .texture_x = 0,
              .texture_y = 0,
              .texture_w = 32,
              .texture_h = 64,
              .render_w = 32,
              .render_h = 64,
              .frames_per_cycle = 0,
              .offset_x = -16,
              .offset_y = -63,
          },
      .final_pixel_digest = std::string(64, '0'),
  };
  recipe.pipeline.composition.canvas_tiles_wide = 1;
  recipe.pipeline.composition.canvas_tiles_high = 2;
  RETURN_IF_ERROR(ValidatePropRecipe(recipe));
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

TEST_F(HeadlessAssetGenerationTest, PropGenerationUsesTheTemplateCanvasAspect) {
  ASSERT_OK_AND_ASSIGN(PropRecipe recipe, PropTemplateRecipe());
  MockApi api;
  EXPECT_CALL(api, GetPropRecipe(recipe.id)).WillOnce(Return(&recipe));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(CreateFakeImageGenerationClient()));
  const std::filesystem::path output = root_ / "prop-candidate";

  ASSERT_OK_AND_ASSIGN(HeadlessAssetGenerationResult result,
                       GenerateAssetCandidateBundle(api, *service,
                                                    {
                                                        .kind = "prop",
                                                        .template_recipe_id = recipe.id,
                                                        .name = "Tall Prop",
                                                        .prompt = "one tall isolated prop",
                                                        .output_path = output.string(),
                                                    }));

  std::ifstream candidate_stream(result.candidate_path);
  nlohmann::json candidate_json;
  candidate_stream >> candidate_json;
  ASSERT_OK_AND_ASSIGN(GeneratedPropCreationCandidate candidate,
                       GeneratedPropCreationCandidateFromJson(candidate_json));
  EXPECT_EQ(candidate.source.width, 64);
  EXPECT_EQ(candidate.source.height, 128);
  EXPECT_EQ(candidate.template_recipe.pipeline.composition.canvas_tiles_wide, 1);
  EXPECT_EQ(candidate.template_recipe.pipeline.composition.canvas_tiles_high, 2);
}

TEST_F(HeadlessAssetGenerationTest, PropGenerationAppliesCanvasAndAttachmentOverrides) {
  ASSERT_OK_AND_ASSIGN(PropRecipe recipe, PropTemplateRecipe());
  MockApi api;
  EXPECT_CALL(api, GetPropRecipe(recipe.id)).WillOnce(Return(&recipe));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(CreateFakeImageGenerationClient()));

  ASSERT_OK_AND_ASSIGN(
      HeadlessAssetGenerationResult result,
      GenerateAssetCandidateBundle(api, *service,
                                   {
                                       .kind = "prop",
                                       .template_recipe_id = recipe.id,
                                       .name = "Wide Ceiling Roots",
                                       .prompt = "hanging cave roots",
                                       .output_path = (root_ / "wide").string(),
                                       .prop_canvas_tiles_wide = 3,
                                       .prop_canvas_tiles_high = 1,
                                       .prop_attachment_mode = PropAttachmentMode::kCeiling,
                                   }));

  std::ifstream candidate_stream(result.candidate_path);
  nlohmann::json candidate_json;
  candidate_stream >> candidate_json;
  ASSERT_OK_AND_ASSIGN(GeneratedPropCreationCandidate candidate,
                       GeneratedPropCreationCandidateFromJson(candidate_json));
  EXPECT_EQ(candidate.template_recipe.pipeline.composition.canvas_tiles_wide, 3);
  EXPECT_EQ(candidate.template_recipe.pipeline.composition.canvas_tiles_high, 1);
  EXPECT_EQ(candidate.template_recipe.pipeline.composition.attachment.mode,
            PropAttachmentMode::kCeiling);
  EXPECT_EQ(candidate.template_recipe.expected_frame.texture_w, 96);
  EXPECT_EQ(candidate.template_recipe.expected_frame.texture_h, 32);
  EXPECT_EQ(candidate.template_recipe.expected_frame.offset_y, 0);
}

TEST_F(HeadlessAssetGenerationTest, RejectsPartialPropCanvasOverrideBeforeRecipeLookup) {
  const absl::Status status = ValidateHeadlessAssetGenerationRequest({
      .kind = "prop",
      .template_recipe_id = "recipe",
      .name = "Partial Canvas",
      .prompt = "broken stones",
      .output_path = (root_ / "partial").string(),
      .prop_canvas_tiles_wide = 3,
  });

  EXPECT_TRUE(absl::IsInvalidArgument(status));
  EXPECT_THAT(status.message(), ::testing::HasSubstr("provided together"));
}

TEST_F(HeadlessAssetGenerationTest, MatteGenerationNamesTheTemplateRecipesExactColor) {
  ASSERT_OK_AND_ASSIGN(ParallaxArtworkRecipe recipe, TemplateRecipe());
  recipe.pipeline.alpha_role = ParallaxArtworkAlphaRole::kTransparentOverlay;
  recipe.pipeline.overlay_extraction = ParallaxArtworkOverlayExtraction::kRemoveSolidMatte;
  recipe.pipeline.overlay_alpha_policy = ParallaxArtworkOverlayAlphaPolicy::kBinary;
  recipe.pipeline.matte_color = {.r = 12, .g = 34, .b = 56, .a = 255};
  recipe.style.palette = {{.r = 7, .g = 8, .b = 9, .a = 255}};
  ASSERT_OK(ValidateParallaxArtworkRecipe(recipe));
  MockApi api;
  EXPECT_CALL(api, GetParallaxArtworkRecipe(recipe.id)).WillOnce(Return(&recipe));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(std::make_unique<InstructionEchoClient>()));

  ASSERT_OK_AND_ASSIGN(HeadlessAssetGenerationResult result,
                       GenerateAssetCandidateBundle(api, *service,
                                                    {
                                                        .kind = "parallax-artwork",
                                                        .template_recipe_id = recipe.id,
                                                        .name = "Matte Cave Overlay",
                                                        .prompt = "a low cave formation",
                                                        .output_path = (root_ / "matte").string(),
                                                    }));

  std::ifstream candidate_stream(result.candidate_path);
  nlohmann::json candidate_json;
  candidate_stream >> candidate_json;
  ASSERT_OK_AND_ASSIGN(GeneratedParallaxArtworkCreationCandidate candidate,
                       GeneratedParallaxArtworkCreationCandidateFromJson(candidate_json));
  ASSERT_TRUE(candidate.source.provenance.revised_prompt.has_value());
  EXPECT_THAT(*candidate.source.provenance.revised_prompt, ::testing::HasSubstr("#0C2238"));
  EXPECT_THAT(*candidate.source.provenance.revised_prompt,
              ::testing::HasSubstr("do not substitute another chroma-key color"));
}

TEST_F(HeadlessAssetGenerationTest,
       ProviderWithoutTransparencyRejectsOverlayRecipeWithoutMatteExtraction) {
  ASSERT_OK_AND_ASSIGN(ParallaxArtworkRecipe recipe, TemplateRecipe());
  recipe.pipeline.alpha_role = ParallaxArtworkAlphaRole::kTransparentOverlay;
  recipe.pipeline.overlay_extraction = ParallaxArtworkOverlayExtraction::kPreserveAlpha;
  recipe.pipeline.overlay_alpha_policy = ParallaxArtworkOverlayAlphaPolicy::kPreserve;
  ASSERT_OK(ValidateParallaxArtworkRecipe(recipe));
  MockApi api;
  EXPECT_CALL(api, GetParallaxArtworkRecipe(recipe.id)).WillOnce(Return(&recipe));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(std::make_unique<InstructionEchoClient>()));

  const absl::Status status =
      GenerateAssetCandidateBundle(api, *service,
                                   {
                                       .kind = "parallax-artwork",
                                       .template_recipe_id = recipe.id,
                                       .name = "Unsupported Alpha Overlay",
                                       .prompt = "a translucent cave formation",
                                       .output_path = (root_ / "alpha").string(),
                                   })
          .status();

  EXPECT_TRUE(absl::IsFailedPrecondition(status));
  EXPECT_THAT(status.message(),
              ::testing::HasSubstr("template recipe cannot remove a solid matte"));
  EXPECT_FALSE(std::filesystem::exists(root_ / "alpha"));
}

TEST_F(HeadlessAssetGenerationTest, StagedSourcePublishesTheSameStrictCreationBundle) {
  ASSERT_OK_AND_ASSIGN(ParallaxArtworkRecipe recipe, TemplateRecipe());
  MockApi api;
  EXPECT_CALL(api, GetParallaxArtworkRecipe(recipe.id)).WillOnce(Return(&recipe));
  RgbaImage source{
      .width = 3,
      .height = 2,
      .pixels =
          {
              1,  2,  3,  255, 4,  5,  6,  255, 7,  8,  9,  255,
              10, 11, 12, 255, 13, 14, 15, 255, 16, 17, 18, 255,
          },
  };
  const std::filesystem::path output = root_ / "staged";

  ASSERT_OK_AND_ASSIGN(HeadlessAssetGenerationResult result,
                       StageAssetCandidateBundle(api, source,
                                                 {
                                                     .kind = "parallax-artwork",
                                                     .template_recipe_id = recipe.id,
                                                     .name = "Imported Cave Background",
                                                     .prompt = "a deep imported cave",
                                                     .provider = "codex-imagegen",
                                                     .model = "gpt-image",
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
  EXPECT_EQ(candidate.source.provenance.provider, "codex-imagegen");
  EXPECT_EQ(candidate.source.provenance.model, "gpt-image");
  ASSERT_OK_AND_ASSIGN(RgbaImage retained,
                       ReadGeneratedAssetSourceCandidate(output, candidate.source));
  EXPECT_EQ(retained.width, source.width);
  EXPECT_EQ(retained.height, source.height);
  EXPECT_EQ(retained.pixels, source.pixels);
}

TEST_F(HeadlessAssetGenerationTest, StagedPropAppliesCanvasAndAttachmentOverrides) {
  ASSERT_OK_AND_ASSIGN(PropRecipe recipe, PropTemplateRecipe());
  MockApi api;
  EXPECT_CALL(api, GetPropRecipe(recipe.id)).WillOnce(Return(&recipe));
  RgbaImage pixels{
      .width = 16,
      .height = 16,
      .pixels = std::vector<uint8_t>(16 * 16 * 4, 255),
  };

  ASSERT_OK_AND_ASSIGN(
      HeadlessAssetGenerationResult result,
      StageAssetCandidateBundle(api, pixels,
                                {
                                    .kind = "prop",
                                    .template_recipe_id = recipe.id,
                                    .name = "Staged Foreground Drapery",
                                    .prompt = "sparse torn drapery",
                                    .provider = "external",
                                    .model = "artist-v1",
                                    .output_path = (root_ / "staged-wide").string(),
                                    .prop_canvas_tiles_wide = 3,
                                    .prop_canvas_tiles_high = 5,
                                    .prop_attachment_mode = PropAttachmentMode::kCeiling,
                                }));

  std::ifstream candidate_stream(result.candidate_path);
  nlohmann::json candidate_json;
  candidate_stream >> candidate_json;
  ASSERT_OK_AND_ASSIGN(GeneratedPropCreationCandidate candidate,
                       GeneratedPropCreationCandidateFromJson(candidate_json));
  EXPECT_EQ(candidate.template_recipe.pipeline.composition.canvas_tiles_wide, 3);
  EXPECT_EQ(candidate.template_recipe.pipeline.composition.canvas_tiles_high, 5);
  EXPECT_EQ(candidate.template_recipe.pipeline.composition.attachment.mode,
            PropAttachmentMode::kCeiling);
}

TEST_F(HeadlessAssetGenerationTest, StagingRejectsAnInvalidInputImageBeforeRecipeLookup) {
  MockApi api;

  const absl::Status status =
      StageAssetCandidateBundle(api, RgbaImage{},
                                {
                                    .kind = "prop",
                                    .template_recipe_id = "template-recipe",
                                    .name = "Invalid Prop",
                                    .prompt = "an invalid prop",
                                    .provider = "codex-imagegen",
                                    .model = "gpt-image",
                                    .output_path = (root_ / "invalid").string(),
                                })
          .status();

  EXPECT_TRUE(absl::IsInvalidArgument(status));
  EXPECT_THAT(status.message(), ::testing::HasSubstr("input image is invalid"));
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
