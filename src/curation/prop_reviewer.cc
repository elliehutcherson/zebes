#include "curation/prop_reviewer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "artwork/prop_recipe.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "curation/prop_candidate.h"
#include "curation/raster_canvas.h"
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
  RETURN_IF_ERROR(DrawRgbaCross(image, origin_x, origin_y, 8, kAnchor));
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
  RETURN_IF_ERROR(DrawRgbaCross(image, origin_x, origin_y, 10, kAnchor));
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

absl::StatusOr<CurationReview> BuildReview(const PropRecipe& recipe, const RgbaImage& texture,
                                           const SpriteFrame& frame,
                                           BlueprintPlacementMode placement_mode,
                                           const GameViewSize& game_view,
                                           const PreparedPropCandidate* candidate) {
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
              {"candidate", candidate != nullptr},
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
  if (candidate != nullptr) {
    review.metadata["requested_recipe"] = candidate->requested_candidate;
    review.metadata["candidate_matches_deterministic_output"] =
        candidate->matches_deterministic_output;
    review.metadata["candidate_operation"] = PropCandidateOperationId(candidate->operation);
    review.metadata["candidate_source_digest"] = candidate->source_content_digest;
    if (!candidate->matches_deterministic_output) {
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
  ASSIGN_OR_RETURN(const BlueprintPlacementMode placement_mode,
                   ResolvePropPlacementMode(*blueprint, sprite->id));

  ASSIGN_OR_RETURN(RgbaImage texture, api.ReadTexturePixels(texture_definition->id));
  return BuildReview(*recipe, texture, sprite->frames.front(), placement_mode,
                     api.GetConfig()->game_view, nullptr);
}

absl::StatusOr<CurationReview> PropReviewer::ReviewCandidate(
    Api& api, const CurationReviewRequest& request, const nlohmann::json& candidate_json) const {
  ASSIGN_OR_RETURN(
      const PreparedPropCandidate candidate,
      PreparePropCandidateForReview(api, request.candidate_root, request.asset_id, candidate_json));
  return BuildReview(candidate.recipe, candidate.texture, candidate.sprite.frames.front(),
                     candidate.placement_mode, api.GetConfig()->game_view, &candidate);
}

absl::Status PropReviewer::CommitCandidate(Api& api, const CurationReviewRequest& request,
                                           const nlohmann::json& candidate_json) const {
  return CommitPropCandidate(api, request.candidate_root, request.asset_id, candidate_json);
}

}  // namespace zebes
