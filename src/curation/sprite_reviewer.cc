#include "curation/sprite_reviewer.h"

#include <string>

#include "absl/status/status.h"
#include "common/status_macros.h"
#include "curation/frame_set_evidence.h"
#include "objects/sprite.h"

namespace zebes {

absl::StatusOr<CurationReview> SpriteReviewer::Review(Api& api,
                                                      const CurationReviewRequest& request) const {
  ASSIGN_OR_RETURN(Sprite * sprite, api.GetSprite(request.asset_id));
  if (sprite == nullptr) return absl::FailedPreconditionError("sprite lookup returned null");
  if (sprite->id.empty() || sprite->name.empty() || sprite->texture_id.empty() ||
      sprite->frames.empty()) {
    return absl::FailedPreconditionError(
        "sprite review needs identity, texture, and at least one frame");
  }
  if (!IsValidSpritePlaybackMode(sprite->playback_mode)) {
    return absl::FailedPreconditionError("sprite review needs a valid playback mode");
  }
  ASSIGN_OR_RETURN(Texture * texture, api.GetTexture(sprite->texture_id));
  if (texture == nullptr || texture->id != sprite->texture_id) {
    return absl::FailedPreconditionError("sprite texture lookup returned invalid data");
  }
  ASSIGN_OR_RETURN(RgbaImage pixels, api.ReadTexturePixels(texture->id));
  RETURN_IF_ERROR(ValidateFrameSetForReview(*sprite, pixels));

  ASSIGN_OR_RETURN(CurationReview review, BuildFrameSetReview({.kind = std::string(kind()),
                                                               .asset_id = sprite->id,
                                                               .asset_name = sprite->name},
                                                              *sprite, pixels));
  RETURN_IF_ERROR(ValidateCurationReview(review));
  return review;
}

}  // namespace zebes
