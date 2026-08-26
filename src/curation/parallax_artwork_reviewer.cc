#include "curation/parallax_artwork_reviewer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/source_artwork_retention.h"
#include "artwork/parallax_artwork_recipe.h"
#include "artwork/prepare_parallax_artwork_asset.h"
#include "artwork/regenerate_parallax_artwork_asset.h"
#include "artwork/repetition_review.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "curation/raster_canvas.h"
#include "generation/generated_asset_candidate.h"

namespace zebes {
namespace {

constexpr size_t kMaximumRepeatPreviewPixels = 64ULL * 1024 * 1024;
constexpr RgbaColor8 kCheckerLight{.red = 55, .green = 55, .blue = 65, .alpha = 255};
constexpr RgbaColor8 kCheckerDark{.red = 35, .green = 35, .blue = 45, .alpha = 255};

absl::StatusOr<ParallaxArtworkRecipe> ParseCandidate(const CurationReviewRequest& request,
                                                     const nlohmann::json& candidate) {
  ASSIGN_OR_RETURN(ParallaxArtworkRecipe recipe, ParallaxArtworkRecipeFromJson(candidate));
  if (recipe.id != request.asset_id) {
    return absl::InvalidArgumentError(absl::StrCat("parallax artwork candidate ID '", recipe.id,
                                                   "' does not match selected ID '",
                                                   request.asset_id, "'"));
  }
  return recipe;
}

absl::StatusOr<PreparedParallaxArtworkRegeneration> PrepareCandidate(
    Api& api, const ParallaxArtworkRecipe& candidate) {
  ASSIGN_OR_RETURN(ParallaxArtworkRecipe * loaded_recipe,
                   api.GetParallaxArtworkRecipe(candidate.id));
  if (loaded_recipe == nullptr) {
    return absl::FailedPreconditionError("parallax artwork recipe lookup returned null");
  }
  const ParallaxArtworkRecipe recipe = *loaded_recipe;
  ASSIGN_OR_RETURN(SourceArtwork * loaded_source, api.GetSourceArtwork(recipe.source_artwork_id));
  ASSIGN_OR_RETURN(Texture * loaded_texture, api.GetTexture(recipe.texture_id));
  if (loaded_source == nullptr || loaded_texture == nullptr) {
    return absl::FailedPreconditionError(
        "parallax artwork regeneration input lookup returned null");
  }
  ASSIGN_OR_RETURN(RgbaImage source_pixels, api.ReadSourceArtworkPixels(recipe.source_artwork_id));
  ASSIGN_OR_RETURN(RgbaImage texture_pixels, api.ReadTexturePixels(recipe.texture_id));
  return PrepareParallaxArtworkRegeneration(*loaded_source, source_pixels, recipe, *loaded_texture,
                                            texture_pixels,
                                            ParallaxArtworkRegenerationSettings{
                                                .terrain_recipe_id = candidate.terrain_recipe_id,
                                                .style = candidate.style,
                                                .pipeline = candidate.pipeline,
                                            });
}

SourceArtwork PreviewSource(const GeneratedParallaxArtworkCreationCandidate& candidate) {
  return SourceArtwork{
      .id = absl::StrCat(candidate.asset_id, "-source-preview"),
      .name = absl::StrCat(candidate.name, " source"),
      .source_path = candidate.source.relative_path,
      .provenance = candidate.source.provenance,
      .width = candidate.source.width,
      .height = candidate.source.height,
      .content_digest = candidate.source.content_digest,
  };
}

PrepareParallaxArtworkAssetRequest CreationRequest(
    const GeneratedParallaxArtworkCreationCandidate& candidate) {
  return PrepareParallaxArtworkAssetRequest{
      .name = candidate.name,
      .terrain_recipe_id = candidate.template_recipe.terrain_recipe_id,
      .style = candidate.template_recipe.style,
      .pipeline = candidate.template_recipe.pipeline,
      .ids = candidate.ids,
  };
}

nlohmann::json EdgeDifferenceToJson(const OpposingEdgeDifference& difference) {
  return {
      {"pixels_compared", difference.pixels_compared},
      {"exact_pixel_matches", difference.exact_pixel_matches},
      {"mean_absolute_channel_difference", difference.mean_absolute_channel_difference},
      {"maximum_channel_difference", difference.maximum_channel_difference},
  };
}

absl::StatusOr<RgbaImage> RenderDetail(const RgbaImage& texture) {
  const int width = std::clamp(texture.width, 640, 1280);
  const int height = std::clamp(texture.height, 360, 720);
  ASSIGN_OR_RETURN(RgbaImage detail,
                   CreateCheckerboardRgbaImage(width, height, 16, kCheckerLight, kCheckerDark));
  const double scale = std::min(static_cast<double>(width) / texture.width,
                                static_cast<double>(height) / texture.height);
  const double rendered_width = texture.width * scale;
  const double rendered_height = texture.height * scale;
  RETURN_IF_ERROR(CompositeRgbaNearest(
      detail, texture, {.x = 0, .y = 0, .width = texture.width, .height = texture.height},
      {.x = (width - rendered_width) / 2.0,
       .y = (height - rendered_height) / 2.0,
       .width = rendered_width,
       .height = rendered_height}));
  return detail;
}

absl::StatusOr<CurationReview> BuildReview(const ParallaxArtworkRecipe& recipe,
                                           const RgbaImage& texture,
                                           std::optional<nlohmann::json> requested_candidate) {
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(recipe));
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(texture));
  if (texture.width != recipe.expected_width || texture.height != recipe.expected_height ||
      digest != recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "parallax artwork texture does not match its recipe dimensions or digest");
  }
  ASSIGN_OR_RETURN(const RepetitionDiagnostics repetition, AnalyzeRepetition(texture));
  ASSIGN_OR_RETURN(RgbaImage detail, RenderDetail(texture));

  CurationReview review{
      .kind = "parallax-artwork",
      .asset_id = recipe.id,
      .asset_name = recipe.name,
      .metadata =
          {
              {"recipe", ParallaxArtworkRecipeToJson(recipe)},
              {"candidate", requested_candidate.has_value()},
              {"texture_id", recipe.texture_id},
              {"rgba_sha256", digest},
              {"repetition",
               {{"horizontal", EdgeDifferenceToJson(repetition.horizontal)},
                {"vertical", EdgeDifferenceToJson(repetition.vertical)}}},
          },
      .artifacts =
          {
              {
                  .id = "texture",
                  .relative_path = "texture.png",
                  .description = "Finished parallax runtime texture at native resolution",
                  .image = texture,
                  .metadata = {{"view", "native"}},
              },
              {
                  .id = "pixel-detail",
                  .relative_path = "pixel-detail.png",
                  .description = "Nearest-neighbour texture view over transparency checks",
                  .image = std::move(detail),
                  .metadata = {{"view", "pixel-detail"}},
              },
          },
  };
  if (recipe.pipeline.review_repeat_x) {
    ASSIGN_OR_RETURN(RgbaImage repeat,
                     BuildRepetitionPreview(texture, 3, 1, kMaximumRepeatPreviewPixels));
    review.artifacts.push_back({
        .id = "repeat-x",
        .relative_path = "repeat-x.png",
        .description = "Three-copy horizontal repetition evidence",
        .image = std::move(repeat),
        .metadata = {{"view", "repeat-x"}, {"copies", 3}},
    });
  }
  if (recipe.pipeline.review_repeat_y) {
    ASSIGN_OR_RETURN(RgbaImage repeat,
                     BuildRepetitionPreview(texture, 1, 3, kMaximumRepeatPreviewPixels));
    review.artifacts.push_back({
        .id = "repeat-y",
        .relative_path = "repeat-y.png",
        .description = "Three-copy vertical repetition evidence",
        .image = std::move(repeat),
        .metadata = {{"view", "repeat-y"}, {"copies", 3}},
    });
  }
  if (requested_candidate.has_value()) {
    review.metadata["requested_recipe"] = *requested_candidate;
    const bool creation = IsGeneratedAssetCreationCandidate(*requested_candidate);
    const bool exact = creation || *requested_candidate == ParallaxArtworkRecipeToJson(recipe);
    review.metadata["candidate_matches_deterministic_output"] = exact;
    review.metadata["candidate_operation"] = creation ? "create" : "regenerate";
    if (!exact) {
      review.findings.push_back({
          .severity = CurationFindingSeverity::kWarning,
          .code = "candidate-recipe-mismatch",
          .subject = recipe.name,
          .message = "the requested recipe does not exactly describe the deterministic output; "
                     "commit will refuse it",
      });
    }
  }
  review.findings.push_back({
      .severity = CurationFindingSeverity::kInfo,
      .code = "edge-difference-measured",
      .subject = recipe.name,
      .message = "opposing-edge differences are recorded as evidence, not a seamlessness verdict",
  });
  RETURN_IF_ERROR(ValidateCurationReview(review));
  return review;
}

}  // namespace

absl::StatusOr<CurationReview> ParallaxArtworkReviewer::Review(
    Api& api, const CurationReviewRequest& request) const {
  ASSIGN_OR_RETURN(ParallaxArtworkRecipe * recipe, api.GetParallaxArtworkRecipe(request.asset_id));
  if (recipe == nullptr) {
    return absl::FailedPreconditionError("parallax artwork recipe lookup returned null");
  }
  ASSIGN_OR_RETURN(Texture * texture, api.GetTexture(recipe->texture_id));
  if (texture == nullptr || texture->id != recipe->texture_id) {
    return absl::FailedPreconditionError("parallax artwork texture lookup returned invalid data");
  }
  ASSIGN_OR_RETURN(RgbaImage pixels, api.ReadTexturePixels(texture->id));
  return BuildReview(*recipe, pixels, std::nullopt);
}

absl::StatusOr<CurationReview> ParallaxArtworkReviewer::ReviewCandidate(
    Api& api, const CurationReviewRequest& request, const nlohmann::json& candidate_json) const {
  if (IsGeneratedAssetCreationCandidate(candidate_json)) {
    ASSIGN_OR_RETURN(const GeneratedParallaxArtworkCreationCandidate candidate,
                     GeneratedParallaxArtworkCreationCandidateFromJson(candidate_json));
    if (candidate.asset_id != request.asset_id) {
      return absl::InvalidArgumentError(
          absl::StrCat("generated parallax artwork candidate ID '", candidate.asset_id,
                       "' does not match selected ID '", request.asset_id, "'"));
    }
    ASSIGN_OR_RETURN(RgbaImage pixels,
                     ReadGeneratedAssetSourceCandidate(request.candidate_root, candidate.source));
    ASSIGN_OR_RETURN(
        PreparedParallaxArtworkAsset prepared,
        PrepareParallaxArtworkAsset(PreviewSource(candidate), pixels, CreationRequest(candidate)));
    return BuildReview(prepared.recipe, prepared.artwork.finished, candidate_json);
  }
  ASSIGN_OR_RETURN(const ParallaxArtworkRecipe candidate, ParseCandidate(request, candidate_json));
  ASSIGN_OR_RETURN(PreparedParallaxArtworkRegeneration prepared, PrepareCandidate(api, candidate));
  return BuildReview(prepared.updated_recipe, prepared.artwork.finished, candidate_json);
}

absl::Status ParallaxArtworkReviewer::CommitCandidate(Api& api,
                                                      const CurationReviewRequest& request,
                                                      const nlohmann::json& candidate_json) const {
  if (IsGeneratedAssetCreationCandidate(candidate_json)) {
    ASSIGN_OR_RETURN(const GeneratedParallaxArtworkCreationCandidate candidate,
                     GeneratedParallaxArtworkCreationCandidateFromJson(candidate_json));
    if (candidate.asset_id != request.asset_id) {
      return absl::InvalidArgumentError(
          absl::StrCat("generated parallax artwork candidate ID '", candidate.asset_id,
                       "' does not match selected ID '", request.asset_id, "'"));
    }
    ASSIGN_OR_RETURN(RgbaImage pixels,
                     ReadGeneratedAssetSourceCandidate(request.candidate_root, candidate.source));
    return RetainSourceArtwork(
               api, absl::StrCat(candidate.name, " source"), candidate.source.provenance, pixels,
               [&api, &candidate](const SourceArtwork& source,
                                  const RgbaImage& retained_pixels) -> absl::Status {
                 ASSIGN_OR_RETURN(PreparedParallaxArtworkAsset prepared,
                                  PrepareParallaxArtworkAsset(source, retained_pixels,
                                                              CreationRequest(candidate)));
                 return api.CreateGeneratedParallaxArtwork(prepared).status();
               })
        .status();
  }
  ASSIGN_OR_RETURN(const ParallaxArtworkRecipe candidate, ParseCandidate(request, candidate_json));
  ASSIGN_OR_RETURN(PreparedParallaxArtworkRegeneration prepared, PrepareCandidate(api, candidate));
  if (candidate.final_pixel_digest != prepared.updated_recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "parallax artwork candidate digest does not match deterministic regenerated pixels");
  }
  if (candidate_json != ParallaxArtworkRecipeToJson(prepared.updated_recipe)) {
    return absl::FailedPreconditionError(
        "parallax artwork candidate changes immutable bundle identity or derived output fields");
  }
  return api.RegenerateGeneratedParallaxArtwork(prepared);
}

}  // namespace zebes
