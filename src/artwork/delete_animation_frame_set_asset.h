#pragma once

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/animation_frame_set_recipe.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "objects/blueprint.h"
#include "objects/sprite.h"
#include "objects/texture.h"

namespace zebes {

// What one Blueprint state should point at once the frame set is gone. An
// empty sprite_id leaves the state unbound, which is what a state that had no
// Sprite before the import returns to.
struct AnimationFrameSetStateRestore {
  std::string state_key;
  std::string sprite_id;

  bool operator==(const AnimationFrameSetStateRestore& other) const = default;
};

struct PreparedAnimationFrameSetDeletion {
  SourceArtwork source_snapshot;
  AnimationFrameSetRecipe recipe_snapshot;
  Texture texture_snapshot;
  RgbaImage texture_pixels_snapshot;
  Sprite sprite_snapshot;
  Blueprint blueprint_snapshot;
  Blueprint updated_blueprint;
  // Carried so validation can re-derive updated_blueprint without the recipe
  // having to store it. In memory for one transaction; never serialized.
  std::vector<AnimationFrameSetStateRestore> state_restore;
};

// Produces the exact Blueprint restoration and complete rollback snapshots
// needed by transactional deletion without touching any store.
//
// `restore` must name exactly the states in `recipe.blueprint_state_keys`. The
// recipe does not record what those states pointed at before it bound them,
// because the only caller that undoes an import is the import itself: it reads
// each Blueprint before rebinding and keeps the prior IDs for the length of the
// run. A caller that cannot supply them has nothing to restore and should not
// be deleting the frame set.
absl::StatusOr<PreparedAnimationFrameSetDeletion> PrepareAnimationFrameSetDeletion(
    const SourceArtwork& source, const AnimationFrameSetRecipe& recipe, const Texture& texture,
    const RgbaImage& texture_pixels, const Sprite& sprite, const Blueprint& blueprint,
    const std::vector<AnimationFrameSetStateRestore>& restore);

absl::Status ValidatePreparedAnimationFrameSetDeletion(
    const PreparedAnimationFrameSetDeletion& prepared);

}  // namespace zebes
