#pragma once

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "curation/review.h"
#include "objects/sprite.h"

namespace zebes {

// Shared visual evidence for one Sprite's frame set: the ordered strip, a
// contact sheet, an alignment overlay, native and enlarged views of every
// frame, and adjacent-frame differences closed either by a loop-closure
// difference or by the held final pose.
//
// A Sprite and the animation frame set that produced it are the same pixels
// seen through different questions, so they publish the same pictures and
// differ only in the verification around them. Keeping the rendering here means
// a clip does not look like two different clips depending on which reviewer
// opened it.

// Opaque extent of one frame's subject in frame-local coordinates. `right` and
// `bottom` are exclusive, so `bottom` compares directly against a contact line.
struct FrameBounds {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  int center_x() const { return (left + right) / 2; }
};

// Names the reviewed asset in the published manifest. The frame set belongs to
// a Sprite, but the asset under review may be the recipe that built it.
struct FrameSetReviewIdentity {
  std::string kind;
  std::string asset_id;
  std::string asset_name;
};

// Rejects a frame that would read outside `texture` or render at a
// non-positive size.
absl::Status ValidateSpriteFrameGeometry(const SpriteFrame& frame, const RgbaImage& texture);

// Enforces the headless artifact limits and frame-index uniqueness the review
// bundle depends on. Callers run this before building evidence so a malformed
// frame set fails before any pixel is rendered.
absl::Status ValidateFrameSetForReview(const Sprite& sprite, const RgbaImage& texture);

// Fails when the frame holds no opaque pixel: an empty frame has no bounds to
// report and nothing to review.
absl::StatusOr<FrameBounds> MeasureFrameBounds(const SpriteFrame& frame, const RgbaImage& texture);

// Builds the complete artifact set and frame/transition metadata. The caller
// adds any findings and domain metadata of its own, then validates and
// publishes.
absl::StatusOr<CurationReview> BuildFrameSetReview(const FrameSetReviewIdentity& identity,
                                                   const Sprite& sprite, const RgbaImage& texture);

}  // namespace zebes
