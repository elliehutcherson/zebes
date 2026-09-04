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
#include "artwork/redraw_parallax_artwork_asset.h"
#include "artwork/regenerate_parallax_artwork_asset.h"
#include "artwork/repetition_review.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "curation/raster_canvas.h"
#include "generation/generated_asset_candidate.h"

namespace zebes {
namespace {

constexpr size_t kMaximumRepeatPreviewPixels = 64ULL * 1024 * 1024;

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

absl::StatusOr<PreparedParallaxArtworkRedraw> PrepareRedrawCandidate(
    Api& api, const CurationReviewRequest& request,
    const GeneratedParallaxArtworkRedrawCandidate& candidate) {
  if (candidate.asset_id != request.asset_id) {
    return absl::InvalidArgumentError(
        absl::StrCat("generated parallax artwork redraw ID '", candidate.asset_id,
                     "' does not match selected ID '", request.asset_id, "'"));
  }
  ASSIGN_OR_RETURN(ParallaxArtworkRecipe * loaded_recipe,
                   api.GetParallaxArtworkRecipe(candidate.asset_id));
  if (loaded_recipe == nullptr) {
    return absl::FailedPreconditionError("parallax artwork recipe lookup returned null");
  }
  const ParallaxArtworkRecipe recipe = *loaded_recipe;
  ASSIGN_OR_RETURN(SourceArtwork * loaded_source, api.GetSourceArtwork(recipe.source_artwork_id));
  ASSIGN_OR_RETURN(Texture * loaded_texture, api.GetTexture(recipe.texture_id));
  if (loaded_source == nullptr || loaded_texture == nullptr) {
    return absl::FailedPreconditionError("parallax artwork redraw input lookup returned null");
  }
  if (loaded_source->content_digest != candidate.expected_source_digest ||
      recipe.final_pixel_digest != candidate.expected_final_pixel_digest) {
    return absl::FailedPreconditionError(
        "parallax artwork changed after this redraw candidate was created");
  }
  ASSIGN_OR_RETURN(RgbaImage source_pixels, api.ReadSourceArtworkPixels(loaded_source->id));
  ASSIGN_OR_RETURN(RgbaImage replacement_pixels,
                   ReadGeneratedAssetSourceCandidate(request.candidate_root, candidate.source));
  ASSIGN_OR_RETURN(RgbaImage texture_pixels, api.ReadTexturePixels(loaded_texture->id));
  return PrepareParallaxArtworkRedraw(*loaded_source, source_pixels, candidate.source.provenance,
                                      replacement_pixels, recipe, *loaded_texture, texture_pixels);
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

struct LateralEdgeOccupancy {
  int gutter_pixels = 0;
  double left = 0.0;
  double right = 0.0;
};

LateralEdgeOccupancy MeasureLateralEdgeOccupancy(const RgbaImage& image) {
  const int gutter = std::max(1, std::min(64, image.width / 12));
  size_t left_visible = 0;
  size_t right_visible = 0;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < gutter; ++x) {
      const size_t left_alpha = (static_cast<size_t>(y) * image.width + x) * 4 + 3;
      const size_t right_alpha =
          (static_cast<size_t>(y) * image.width + image.width - 1 - x) * 4 + 3;
      if (image.pixels[left_alpha] != 0) ++left_visible;
      if (image.pixels[right_alpha] != 0) ++right_visible;
    }
  }
  const double sample_count = static_cast<double>(gutter) * image.height;
  return {
      .gutter_pixels = gutter,
      .left = left_visible / sample_count,
      .right = right_visible / sample_count,
  };
}

absl::StatusOr<RgbaImage> RenderDetail(const RgbaImage& texture) {
  const int width = std::clamp(texture.width, 640, 1280);
  const int height = std::clamp(texture.height, 360, 720);
  ASSIGN_OR_RETURN(
      RgbaImage detail,
      CreateCheckerboardRgbaImage(width, height, 16, kReviewCheckerLight, kReviewCheckerDark));
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
  const LateralEdgeOccupancy occupancy = MeasureLateralEdgeOccupancy(texture);
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
              {"lateral_edge_occupancy",
               {{"gutter_pixels", occupancy.gutter_pixels},
                {"left", occupancy.left},
                {"right", occupancy.right}}},
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
    const bool redraw = IsGeneratedAssetRedrawCandidate(*requested_candidate);
    const bool exact =
        creation || redraw || *requested_candidate == ParallaxArtworkRecipeToJson(recipe);
    review.metadata["candidate_matches_deterministic_output"] = exact;
    review.metadata["candidate_operation"] =
        creation ? "create" : (redraw ? "redraw" : "regenerate");
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
  const bool exact_horizontal_seam =
      repetition.horizontal.exact_pixel_matches == repetition.horizontal.pixels_compared &&
      repetition.horizontal.maximum_channel_difference == 0;
  if (recipe.pipeline.alpha_role == ParallaxArtworkAlphaRole::kTransparentOverlay &&
      !exact_horizontal_seam && (occupancy.left > 0.1 || occupancy.right > 0.1)) {
    review.findings.push_back({
        .severity = CurationFindingSeverity::kWarning,
        .code = "hard-horizontal-edge",
        .subject = recipe.name,
        .message =
            "visible pixels occupy more than 10% of a lateral review gutter; taper the formation "
            "into transparency or use it only where the edge is guaranteed to remain hidden",
    });
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
  if (IsGeneratedAssetRedrawCandidate(candidate_json)) {
    ASSIGN_OR_RETURN(const GeneratedParallaxArtworkRedrawCandidate candidate,
                     GeneratedParallaxArtworkRedrawCandidateFromJson(candidate_json));
    ASSIGN_OR_RETURN(PreparedParallaxArtworkRedraw prepared,
                     PrepareRedrawCandidate(api, request, candidate));
    return BuildReview(prepared.updated_recipe, prepared.artwork.finished, candidate_json);
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
  if (IsGeneratedAssetRedrawCandidate(candidate_json)) {
    ASSIGN_OR_RETURN(const GeneratedParallaxArtworkRedrawCandidate candidate,
                     GeneratedParallaxArtworkRedrawCandidateFromJson(candidate_json));
    ASSIGN_OR_RETURN(PreparedParallaxArtworkRedraw prepared,
                     PrepareRedrawCandidate(api, request, candidate));
    return api.RedrawGeneratedParallaxArtwork(prepared);
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
