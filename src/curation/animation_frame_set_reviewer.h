#pragma once

#include <string_view>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "curation/registry.h"

namespace zebes {

// Reviews a committed animation frame set through its recipe rather than
// through the Sprite the recipe produced.
//
// The `sprite` kind already answers what the frames look like, and this
// reviewer publishes exactly the same evidence for them. What it adds is the
// question a Sprite cannot answer: whether the Texture, Sprite, and Blueprint
// state the recipe owns are still the ones it built. A texture repainted in
// place, a frame table edited by hand, or a Blueprint state rebound to another
// Sprite all leave a Sprite review looking perfectly healthy while the recipe
// silently stops describing the asset. Each of those fails here instead.
//
// Registration findings are measured against the recipe's declared contract —
// which frames are planted, how far a subject may drift — which the Sprite
// alone does not record. Identity, anatomy, and pose quality stay visible
// judgements; the 2026-08-29 gate established that structural checks pass
// clips that fail live playback.
class AnimationFrameSetReviewer : public CurationReviewer {
 public:
  std::string_view kind() const override { return "animation-frame-set"; }

  absl::StatusOr<CurationReview> Review(Api& api,
                                        const CurationReviewRequest& request) const override;
};

}  // namespace zebes
