#include "curation/animation_frame_set_reviewer.h"

#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "artwork/animation_frame_set_recipe.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "curation/frame_set_evidence.h"
#include "nlohmann/json.hpp"
#include "objects/blueprint.h"
#include "objects/sprite.h"

namespace zebes {
namespace {

// Fails when the persisted graph has stopped matching the recipe that owns it.
// This is deliberately an error rather than a finding: once the recipe no
// longer describes the asset, every measurement below it is describing
// something the recipe cannot rebuild, and publishing that evidence would hide
// the drift behind a healthy-looking review.
absl::Status CheckPersistedGraph(const AnimationFrameSetRecipe& recipe, const Texture& texture,
                                 const Sprite& sprite, const Blueprint& blueprint,
                                 const std::string& pixel_digest) {
  if (sprite.texture_id != texture.id) {
    return absl::FailedPreconditionError(
        "animation frame set sprite no longer references the recipe texture");
  }
  if (pixel_digest != recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "animation frame set texture pixels no longer match the recipe digest");
  }
  if (sprite.frames != recipe.expected_frames) {
    return absl::FailedPreconditionError(
        "animation frame set sprite frames no longer match the recipe");
  }
  if (sprite.playback_mode != recipe.pipeline.playback_mode) {
    return absl::FailedPreconditionError(
        "animation frame set sprite playback mode no longer matches the recipe");
  }
  for (const AnimationFrameSetBlueprintBinding& binding : recipe.blueprint_bindings) {
    const std::optional<int> index = blueprint.state_index(binding.state_key);
    if (!index.has_value()) {
      return absl::FailedPreconditionError(absl::StrCat(
          "animation frame set recipe binds missing Blueprint state ", binding.state_key));
    }
    if (blueprint.sprite_id(*index) != recipe.sprite_id) {
      return absl::FailedPreconditionError(
          absl::StrCat("Blueprint state ", binding.state_key,
                       " no longer binds the animation frame set sprite"));
    }
  }
  return absl::OkStatus();
}

// Measures each frame against the registration the recipe declared. The Sprite
// carries an origin in its frame offsets but not which frames are planted or
// how far a subject may drift, so these findings are only available here.
absl::Status AddRegistrationFindings(const AnimationFrameSetRecipe& recipe, const Sprite& sprite,
                                     const RgbaImage& texture, CurationReview& review) {
  const AnimationFrameSetPipelineConfig& pipeline = recipe.pipeline;
  nlohmann::json registration = nlohmann::json::array();
  for (size_t index = 0; index < sprite.frames.size(); ++index) {
    const SpriteFrame& frame = sprite.frames[index];
    ASSIGN_OR_RETURN(const FrameBounds bounds, MeasureFrameBounds(frame, texture));
    const bool planted = index < pipeline.planted_frames.size() && pipeline.planted_frames[index];
    const int horizontal_drift = bounds.center_x() - pipeline.origin_x;
    const int vertical_drift = bounds.bottom - pipeline.contact_line_y;
    registration.push_back({
        {"index", frame.index},
        {"planted", planted},
        {"horizontal_anchor_drift", horizontal_drift},
        {"vertical_anchor_drift", vertical_drift},
    });
    if (planted && std::abs(vertical_drift) > pipeline.contact_tolerance) {
      review.findings.push_back({
          .severity = CurationFindingSeverity::kWarning,
          .code = "planted-frame-misses-contact",
          .subject = absl::StrCat("frame ", frame.index),
          .message = absl::StrCat("a planted frame sits ", vertical_drift,
                                  " pixels from the declared contact line; the foot will "
                                  "slide or float during playback"),
      });
    }
    if (std::abs(horizontal_drift) > pipeline.maximum_horizontal_anchor_drift) {
      review.findings.push_back({
          .severity = CurationFindingSeverity::kWarning,
          .code = "horizontal-anchor-drift",
          .subject = absl::StrCat("frame ", frame.index),
          .message = absl::StrCat("the subject center is ", horizontal_drift,
                                  " pixels from the declared origin; the clip will shift "
                                  "sideways during playback"),
      });
    }
  }
  review.metadata["registration"] = std::move(registration);
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<CurationReview> AnimationFrameSetReviewer::Review(
    Api& api, const CurationReviewRequest& request) const {
  ASSIGN_OR_RETURN(AnimationFrameSetRecipe * recipe,
                   api.GetAnimationFrameSetRecipe(request.asset_id));
  if (recipe == nullptr) {
    return absl::FailedPreconditionError("animation frame set recipe lookup returned null");
  }
  RETURN_IF_ERROR(ValidateAnimationFrameSetRecipe(*recipe));

  ASSIGN_OR_RETURN(Texture * texture_definition, api.GetTexture(recipe->texture_id));
  ASSIGN_OR_RETURN(Sprite * sprite, api.GetSprite(recipe->sprite_id));
  ASSIGN_OR_RETURN(Blueprint * blueprint, api.GetBlueprint(recipe->blueprint_id));
  if (texture_definition == nullptr || sprite == nullptr || blueprint == nullptr) {
    return absl::FailedPreconditionError(
        "animation frame set bundle lookup returned a null definition");
  }
  ASSIGN_OR_RETURN(RgbaImage pixels, api.ReadTexturePixels(texture_definition->id));
  ASSIGN_OR_RETURN(const std::string pixel_digest, RgbaImageDigest(pixels));
  RETURN_IF_ERROR(
      CheckPersistedGraph(*recipe, *texture_definition, *sprite, *blueprint, pixel_digest));
  RETURN_IF_ERROR(ValidateFrameSetForReview(*sprite, pixels));

  ASSIGN_OR_RETURN(CurationReview review, BuildFrameSetReview({.kind = std::string(kind()),
                                                               .asset_id = recipe->id,
                                                               .asset_name = recipe->name},
                                                              *sprite, pixels));
  review.metadata["recipe"] = AnimationFrameSetRecipeToJson(*recipe);
  review.metadata["sprite_id"] = recipe->sprite_id;
  review.metadata["blueprint_id"] = recipe->blueprint_id;
  review.metadata["source_artwork_id"] = recipe->source_artwork_id;
  review.metadata["rgba_sha256"] = pixel_digest;
  RETURN_IF_ERROR(AddRegistrationFindings(*recipe, *sprite, pixels, review));

  RETURN_IF_ERROR(ValidateCurationReview(review));
  return review;
}

}  // namespace zebes
