#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/animation_frame_set_recipe.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "objects/blueprint.h"
#include "objects/sprite.h"
#include "objects/texture.h"

namespace zebes {

struct PreparedAnimationFrameSetDeletion {
  SourceArtwork source_snapshot;
  AnimationFrameSetRecipe recipe_snapshot;
  Texture texture_snapshot;
  RgbaImage texture_pixels_snapshot;
  Sprite sprite_snapshot;
  Blueprint blueprint_snapshot;
  Blueprint updated_blueprint;
};

// Produces the exact Blueprint restoration and complete rollback snapshots
// needed by transactional deletion without touching any store.
absl::StatusOr<PreparedAnimationFrameSetDeletion> PrepareAnimationFrameSetDeletion(
    const SourceArtwork& source, const AnimationFrameSetRecipe& recipe, const Texture& texture,
    const RgbaImage& texture_pixels, const Sprite& sprite, const Blueprint& blueprint);

absl::Status ValidatePreparedAnimationFrameSetDeletion(
    const PreparedAnimationFrameSetDeletion& prepared);

}  // namespace zebes
