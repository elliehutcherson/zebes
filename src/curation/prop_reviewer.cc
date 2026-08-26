#include "curation/prop_reviewer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/source_artwork_retention.h"
#include "artwork/prepare_prop_asset.h"
#include "artwork/prop_recipe.h"
#include "artwork/regenerate_prop_asset.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "curation/raster_canvas.h"
#include "generation/generated_asset_candidate.h"
#include "objects/blueprint.h"
#include "objects/sprite.h"

namespace zebes {
namespace {

constexpr RgbaColor8 kCheckerLight{.red = 55, .green = 55, .blue = 65, .alpha = 255};
constexpr RgbaColor8 kCheckerDark{.red = 35, .green = 35, .blue = 45, .alpha = 255};
constexpr RgbaColor8 kGuide{.red = 255, .green = 190, .blue = 50, .alpha = 255};
constexpr RgbaColor8 kAnchor{.red = 80, .green = 220, .blue = 255, .alpha = 255};

absl::Status ValidateFrame(const SpriteFrame& frame, const RgbaImage& texture) {
  const int64_t source_right = static_cast<int64_t>(frame.texture_x) + frame.texture_w;
  const int64_t source_bottom = static_cast<int64_t>(frame.texture_y) + frame.texture_h;
  if (frame.texture_x < 0 || frame.texture_y < 0 || frame.texture_w <= 0 || frame.texture_h <= 0 ||
      frame.render_w <= 0 || frame.render_h <= 0 || source_right > texture.width ||
      source_bottom > texture.height) {
    return absl::FailedPreconditionError("prop sprite frame exceeds its texture or render bounds");
  }
  return absl::OkStatus();
}

const Blueprint::State* FindPropState(const Blueprint& blueprint, const std::string& sprite_id) {
  for (const Blueprint::State& state : blueprint.states) {
    if (state.sprite_id == sprite_id) return &state;
  }
  return nullptr;
}

absl::Status DrawCross(RgbaImage& image, int x, int y, int radius, RgbaColor8 color) {
  RETURN_IF_ERROR(FillRgbaRect(image, x - radius, y, radius * 2 + 1, 1, color));
  return FillRgbaRect(image, x, y - radius, 1, radius * 2 + 1, color);
}

absl::StatusOr<RgbaImage> RenderPlacementContext(const RgbaImage& texture, const SpriteFrame& frame,
                                                 BlueprintPlacementMode placement_mode,
                                                 const GameViewSize& game_view) {
  ASSIGN_OR_RETURN(RgbaImage image, CreateCheckerboardRgbaImage(game_view.width, game_view.height,
                                                                16, kCheckerLight, kCheckerDark));
  const int origin_x = game_view.width / 2;
  int origin_y = game_view.height / 2;
  if (placement_mode == BlueprintPlacementMode::kGrounded) {
    origin_y = game_view.height * 3 / 4;
  } else if (placement_mode == BlueprintPlacementMode::kCeiling) {
    origin_y = game_view.height / 4;
  }
  if (placement_mode != BlueprintPlacementMode::kFree) {
    RETURN_IF_ERROR(FillRgbaRect(image, 0, origin_y, game_view.width, 2, kGuide));
  }
  RETURN_IF_ERROR(CompositeRgbaNearest(image, texture,
                                       {.x = frame.texture_x,
                                        .y = frame.texture_y,
                                        .width = frame.texture_w,
                                        .height = frame.texture_h},
                                       {.x = static_cast<double>(origin_x + frame.offset_x),
                                        .y = static_cast<double>(origin_y + frame.offset_y),
                                        .width = static_cast<double>(frame.render_w),
                                        .height = static_cast<double>(frame.render_h)}));
  RETURN_IF_ERROR(DrawCross(image, origin_x, origin_y, 8, kAnchor));
  return image;
}

absl::StatusOr<RgbaImage> RenderPixelDetail(const RgbaImage& texture, const SpriteFrame& frame,
                                            const GameViewSize& game_view) {
  ASSIGN_OR_RETURN(RgbaImage image, CreateCheckerboardRgbaImage(game_view.width, game_view.height,
                                                                16, kCheckerLight, kCheckerDark));
  const int fit_x = std::max(1, (game_view.width - 64) / frame.render_w);
  const int fit_y = std::max(1, (game_view.height - 64) / frame.render_h);
  const int scale = std::clamp(std::min(fit_x, fit_y), 1, 8);
  const int origin_x = game_view.width / 2;
  const int origin_y = game_view.height / 2;
  RETURN_IF_ERROR(CompositeRgbaNearest(image, texture,
                                       {.x = frame.texture_x,
                                        .y = frame.texture_y,
                                        .width = frame.texture_w,
                                        .height = frame.texture_h},
                                       {.x = static_cast<double>(origin_x + frame.offset_x * scale),
                                        .y = static_cast<double>(origin_y + frame.offset_y * scale),
                                        .width = static_cast<double>(frame.render_w * scale),
                                        .height = static_cast<double>(frame.render_h * scale)}));
  RETURN_IF_ERROR(DrawCross(image, origin_x, origin_y, 10, kAnchor));
  return image;
}

bool OpaquePixel(const RgbaImage& image, int x, int y) {
  return image.pixels[(static_cast<size_t>(y) * image.width + x) * 4 + 3] != 0;
}

bool TouchesTextureEdge(const RgbaImage& image) {
  for (int x = 0; x < image.width; ++x) {
    if (OpaquePixel(image, x, 0) || OpaquePixel(image, x, image.height - 1)) return true;
  }
  for (int y = 0; y < image.height; ++y) {
    if (OpaquePixel(image, 0, y) || OpaquePixel(image, image.width - 1, y)) return true;
  }
  return false;
}

absl::StatusOr<PropRecipe> ParseCandidate(const CurationReviewRequest& request,
                                          const nlohmann::json& candidate) {
  ASSIGN_OR_RETURN(PropRecipe recipe, PropRecipeFromJson(candidate));
  if (recipe.id != request.asset_id) {
    return absl::InvalidArgumentError(absl::StrCat(
        "prop candidate ID '", recipe.id, "' does not match selected ID '", request.asset_id, "'"));
  }
  if (PropRecipeToJson(recipe) != candidate) {
    return absl::InvalidArgumentError(
        "prop candidate must be one exact schema-current recipe object");
  }
  return recipe;
}

absl::StatusOr<PreparedPropRegeneration> PrepareCandidate(Api& api, const PropRecipe& candidate) {
  ASSIGN_OR_RETURN(PropRecipe * loaded_recipe, api.GetPropRecipe(candidate.id));
  if (loaded_recipe == nullptr) {
    return absl::FailedPreconditionError("prop recipe lookup returned null");
  }
  const PropRecipe recipe = *loaded_recipe;
  ASSIGN_OR_RETURN(SourceArtwork * loaded_source, api.GetSourceArtwork(recipe.source_artwork_id));
  ASSIGN_OR_RETURN(Texture * loaded_texture, api.GetTexture(recipe.texture_id));
  ASSIGN_OR_RETURN(Sprite * loaded_sprite, api.GetSprite(recipe.sprite_id));
  if (loaded_source == nullptr || loaded_texture == nullptr || loaded_sprite == nullptr) {
    return absl::FailedPreconditionError("prop regeneration input lookup returned null");
  }
  ASSIGN_OR_RETURN(RgbaImage source_pixels, api.ReadSourceArtworkPixels(recipe.source_artwork_id));
  ASSIGN_OR_RETURN(RgbaImage texture_pixels, api.ReadTexturePixels(recipe.texture_id));
  return PreparePropRegeneration(*loaded_source, source_pixels, recipe, *loaded_texture,
                                 texture_pixels, *loaded_sprite,
                                 PropRegenerationSettings{
                                     .terrain_recipe_id = candidate.terrain_recipe_id,
                                     .style = candidate.style,
                                     .pipeline = candidate.pipeline,
                                 });
}

SourceArtwork PreviewSource(const GeneratedPropCreationCandidate& candidate) {
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

PreparePropAssetRequest CreationRequest(const GeneratedPropCreationCandidate& candidate) {
  return PreparePropAssetRequest{
      .name = candidate.name,
      .terrain_recipe_id = candidate.template_recipe.terrain_recipe_id,
      .style = candidate.template_recipe.style,
      .pipeline = candidate.template_recipe.pipeline,
      .ids = candidate.ids,
  };
}

absl::StatusOr<CurationReview> BuildReview(const PropRecipe& recipe, const RgbaImage& texture,
                                           const SpriteFrame& frame,
                                           BlueprintPlacementMode placement_mode,
                                           const GameViewSize& game_view,
                                           std::optional<nlohmann::json> requested_candidate) {
  RETURN_IF_ERROR(ValidatePropRecipe(recipe));
  RETURN_IF_ERROR(ValidateFrame(frame, texture));
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(texture));
  if (digest != recipe.final_pixel_digest) {
    return absl::FailedPreconditionError("prop texture pixels do not match the recipe digest");
  }
  if (!game_view.IsValid()) return absl::FailedPreconditionError("game view is invalid");

  ASSIGN_OR_RETURN(RgbaImage placement,
                   RenderPlacementContext(texture, frame, placement_mode, game_view));
  ASSIGN_OR_RETURN(RgbaImage detail, RenderPixelDetail(texture, frame, game_view));

  CurationReview review{
      .kind = "prop",
      .asset_id = recipe.id,
      .asset_name = recipe.name,
      .metadata =
          {
              {"recipe", PropRecipeToJson(recipe)},
              {"candidate", requested_candidate.has_value()},
              {"texture_id", recipe.texture_id},
              {"sprite_id", recipe.sprite_id},
              {"blueprint_id", recipe.blueprint_id},
              {"placement_mode", BlueprintPlacementModeId(placement_mode)},
              {"rgba_sha256", digest},
              {"game_view", {{"width", game_view.width}, {"height", game_view.height}}},
          },
      .artifacts =
          {
              {
                  .id = "texture",
                  .relative_path = "texture.png",
                  .description = "Finished runtime texture at native resolution",
                  .image = texture,
                  .metadata = {{"view", "native"}},
              },
              {
                  .id = "pixel-detail",
                  .relative_path = "pixel-detail.png",
                  .description = "Nearest-neighbour enlarged artwork with authored origin",
                  .image = std::move(detail),
                  .metadata = {{"view", "pixel-detail"}},
              },
              {
                  .id = "placement-context",
                  .relative_path = "placement-context.png",
                  .description = "Logical game-view placement using sprite offsets and attachment",
                  .image = std::move(placement),
                  .metadata = {{"view", "placement-context"}},
              },
          },
  };
  if (requested_candidate.has_value()) {
    review.metadata["requested_recipe"] = *requested_candidate;
    const bool creation = IsGeneratedAssetCreationCandidate(*requested_candidate);
    const bool exact = creation || *requested_candidate == PropRecipeToJson(recipe);
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
  if (TouchesTextureEdge(texture)) {
    review.findings.push_back({
        .severity = CurationFindingSeverity::kWarning,
        .code = "subject-touches-texture-edge",
        .subject = recipe.name,
        .message = "opaque artwork touches the runtime texture edge; inspect for clipping",
    });
  } else {
    review.findings.push_back({
        .severity = CurationFindingSeverity::kInfo,
        .code = "transparent-edge-clearance",
        .subject = recipe.name,
        .message = "the runtime texture has transparent clearance on every edge",
    });
  }
  RETURN_IF_ERROR(ValidateCurationReview(review));
  return review;
}

}  // namespace

absl::StatusOr<CurationReview> PropReviewer::Review(Api& api,
                                                    const CurationReviewRequest& request) const {
  ASSIGN_OR_RETURN(PropRecipe * recipe, api.GetPropRecipe(request.asset_id));
  if (recipe == nullptr) return absl::FailedPreconditionError("prop recipe lookup returned null");
  RETURN_IF_ERROR(ValidatePropRecipe(*recipe));

  ASSIGN_OR_RETURN(Texture * texture_definition, api.GetTexture(recipe->texture_id));
  ASSIGN_OR_RETURN(Sprite * sprite, api.GetSprite(recipe->sprite_id));
  ASSIGN_OR_RETURN(Blueprint * blueprint, api.GetBlueprint(recipe->blueprint_id));
  if (texture_definition == nullptr || sprite == nullptr || blueprint == nullptr) {
    return absl::FailedPreconditionError("prop bundle lookup returned a null definition");
  }
  if (sprite->texture_id != texture_definition->id || sprite->frames.size() != 1 ||
      sprite->frames.front() != recipe->expected_frame) {
    return absl::FailedPreconditionError("prop recipe no longer matches its texture and sprite");
  }
  const Blueprint::State* state = FindPropState(*blueprint, sprite->id);
  if (state == nullptr || !IsValidBlueprintPlacementMode(state->placement_mode)) {
    return absl::FailedPreconditionError("prop blueprint has no valid state for its sprite");
  }

  ASSIGN_OR_RETURN(RgbaImage texture, api.ReadTexturePixels(texture_definition->id));
  return BuildReview(*recipe, texture, sprite->frames.front(), state->placement_mode,
                     api.GetConfig()->game_view, std::nullopt);
}

absl::StatusOr<CurationReview> PropReviewer::ReviewCandidate(
    Api& api, const CurationReviewRequest& request, const nlohmann::json& candidate_json) const {
  if (IsGeneratedAssetCreationCandidate(candidate_json)) {
    ASSIGN_OR_RETURN(const GeneratedPropCreationCandidate candidate,
                     GeneratedPropCreationCandidateFromJson(candidate_json));
    if (candidate.asset_id != request.asset_id) {
      return absl::InvalidArgumentError(
          absl::StrCat("generated prop candidate ID '", candidate.asset_id,
                       "' does not match selected ID '", request.asset_id, "'"));
    }
    ASSIGN_OR_RETURN(RgbaImage pixels,
                     ReadGeneratedAssetSourceCandidate(request.candidate_root, candidate.source));
    ASSIGN_OR_RETURN(PreparedPropAsset prepared, PreparePropAsset(PreviewSource(candidate), pixels,
                                                                  CreationRequest(candidate)));
    const Blueprint::State* state = FindPropState(prepared.blueprint, prepared.sprite.id);
    if (state == nullptr || !IsValidBlueprintPlacementMode(state->placement_mode)) {
      return absl::FailedPreconditionError(
          "generated prop candidate produced no valid blueprint state");
    }
    return BuildReview(prepared.recipe, prepared.artwork.finished.image,
                       prepared.sprite.frames.front(), state->placement_mode,
                       api.GetConfig()->game_view, candidate_json);
  }
  ASSIGN_OR_RETURN(const PropRecipe candidate, ParseCandidate(request, candidate_json));
  ASSIGN_OR_RETURN(PreparedPropRegeneration prepared, PrepareCandidate(api, candidate));
  ASSIGN_OR_RETURN(Blueprint * blueprint, api.GetBlueprint(prepared.updated_recipe.blueprint_id));
  if (blueprint == nullptr) {
    return absl::FailedPreconditionError("prop blueprint lookup returned null");
  }
  const Blueprint::State* state = FindPropState(*blueprint, prepared.updated_sprite.id);
  if (state == nullptr || !IsValidBlueprintPlacementMode(state->placement_mode)) {
    return absl::FailedPreconditionError("prop blueprint has no valid state for its sprite");
  }
  return BuildReview(prepared.updated_recipe, prepared.artwork.finished.image,
                     prepared.updated_sprite.frames.front(), state->placement_mode,
                     api.GetConfig()->game_view, candidate_json);
}

absl::Status PropReviewer::CommitCandidate(Api& api, const CurationReviewRequest& request,
                                           const nlohmann::json& candidate_json) const {
  if (IsGeneratedAssetCreationCandidate(candidate_json)) {
    ASSIGN_OR_RETURN(const GeneratedPropCreationCandidate candidate,
                     GeneratedPropCreationCandidateFromJson(candidate_json));
    if (candidate.asset_id != request.asset_id) {
      return absl::InvalidArgumentError(
          absl::StrCat("generated prop candidate ID '", candidate.asset_id,
                       "' does not match selected ID '", request.asset_id, "'"));
    }
    ASSIGN_OR_RETURN(RgbaImage pixels,
                     ReadGeneratedAssetSourceCandidate(request.candidate_root, candidate.source));
    return RetainSourceArtwork(
               api, absl::StrCat(candidate.name, " source"), candidate.source.provenance, pixels,
               [&api, &candidate](const SourceArtwork& source,
                                  const RgbaImage& retained_pixels) -> absl::Status {
                 ASSIGN_OR_RETURN(
                     PreparedPropAsset prepared,
                     PreparePropAsset(source, retained_pixels, CreationRequest(candidate)));
                 return api.CreateGeneratedProp(prepared).status();
               })
        .status();
  }
  ASSIGN_OR_RETURN(const PropRecipe candidate, ParseCandidate(request, candidate_json));
  ASSIGN_OR_RETURN(PreparedPropRegeneration prepared, PrepareCandidate(api, candidate));
  if (candidate.final_pixel_digest != prepared.updated_recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "prop candidate digest does not match the deterministic regenerated pixels");
  }
  if (candidate_json != PropRecipeToJson(prepared.updated_recipe)) {
    return absl::FailedPreconditionError(
        "prop candidate changes immutable bundle identity or derived output fields");
  }
  return api.RegenerateGeneratedProp(prepared);
}

}  // namespace zebes
