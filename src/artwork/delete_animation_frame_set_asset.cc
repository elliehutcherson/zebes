#include "artwork/delete_animation_frame_set_asset.h"

#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/image_digest.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

std::string TexturePath(std::string_view texture_id) {
  return absl::StrCat("textures/animation_frame_sets/", texture_id, ".png");
}

absl::StatusOr<size_t> FindState(const Blueprint& blueprint, std::string_view state_key) {
  for (size_t index = 0; index < blueprint.states.size(); ++index) {
    if (blueprint.states[index].key == state_key) return index;
  }
  return absl::NotFoundError(absl::StrCat("Blueprint has no state with key '", state_key, "'"));
}

absl::Status ValidateGraph(const PreparedAnimationFrameSetDeletion& prepared) {
  const AnimationFrameSetRecipe& recipe = prepared.recipe_snapshot;
  if (recipe.source_artwork_id != prepared.source_snapshot.id ||
      recipe.texture_id != prepared.texture_snapshot.id ||
      recipe.sprite_id != prepared.sprite_snapshot.id ||
      recipe.blueprint_id != prepared.blueprint_snapshot.id) {
    return absl::InvalidArgumentError(
        "animation frame-set deletion snapshots do not match recipe bindings");
  }
  if (prepared.texture_snapshot.name != recipe.name ||
      prepared.texture_snapshot.path != TexturePath(recipe.texture_id)) {
    return absl::FailedPreconditionError(
        "animation frame-set deletion Texture differs from its recipe");
  }
  if (prepared.sprite_snapshot.name != recipe.name ||
      prepared.sprite_snapshot.texture_id != recipe.texture_id ||
      prepared.sprite_snapshot.playback_mode != recipe.pipeline.playback_mode ||
      prepared.sprite_snapshot.frames != recipe.expected_frames) {
    return absl::FailedPreconditionError(
        "animation frame-set deletion Sprite differs from its recipe");
  }

  Blueprint expected = prepared.blueprint_snapshot;
  for (const AnimationFrameSetBlueprintBinding& binding : recipe.blueprint_bindings) {
    ASSIGN_OR_RETURN(const size_t state_index, FindState(expected, binding.state_key));
    if (expected.states[state_index].sprite_id != recipe.sprite_id) {
      return absl::FailedPreconditionError(
          absl::StrCat("Blueprint state '", binding.state_key,
                       "' no longer has the recipe-owned animation Sprite binding"));
    }
    expected.states[state_index].sprite_id = binding.previous_sprite_id;
  }
  if (prepared.updated_blueprint != expected) {
    return absl::InvalidArgumentError(
        "animation frame-set deletion modifies non-binding Blueprint state");
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<PreparedAnimationFrameSetDeletion> PrepareAnimationFrameSetDeletion(
    const SourceArtwork& source, const AnimationFrameSetRecipe& recipe, const Texture& texture,
    const RgbaImage& texture_pixels, const Sprite& sprite, const Blueprint& blueprint) {
  Blueprint updated_blueprint = blueprint;
  for (const AnimationFrameSetBlueprintBinding& binding : recipe.blueprint_bindings) {
    ASSIGN_OR_RETURN(const size_t state_index, FindState(updated_blueprint, binding.state_key));
    if (updated_blueprint.states[state_index].sprite_id != recipe.sprite_id) {
      return absl::FailedPreconditionError(
          absl::StrCat("Blueprint state '", binding.state_key,
                       "' no longer has the recipe-owned animation Sprite binding"));
    }
    updated_blueprint.states[state_index].sprite_id = binding.previous_sprite_id;
  }
  PreparedAnimationFrameSetDeletion prepared{
      .source_snapshot = source,
      .recipe_snapshot = recipe,
      .texture_snapshot = texture,
      .texture_pixels_snapshot = texture_pixels,
      .sprite_snapshot = sprite,
      .blueprint_snapshot = blueprint,
      .updated_blueprint = std::move(updated_blueprint),
  };
  RETURN_IF_ERROR(ValidatePreparedAnimationFrameSetDeletion(prepared));
  return prepared;
}

absl::Status ValidatePreparedAnimationFrameSetDeletion(
    const PreparedAnimationFrameSetDeletion& prepared) {
  RETURN_IF_ERROR(ValidateSourceArtwork(prepared.source_snapshot));
  RETURN_IF_ERROR(ValidateAnimationFrameSetRecipe(prepared.recipe_snapshot));
  if (!prepared.texture_pixels_snapshot.IsValid()) {
    return absl::InvalidArgumentError("animation frame-set deletion has invalid Texture pixels");
  }
  ASSIGN_OR_RETURN(const std::string texture_digest,
                   RgbaImageDigest(prepared.texture_pixels_snapshot));
  if (texture_digest != prepared.recipe_snapshot.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "animation frame-set deletion Texture pixels differ from its recipe");
  }
  return ValidateGraph(prepared);
}

}  // namespace zebes
