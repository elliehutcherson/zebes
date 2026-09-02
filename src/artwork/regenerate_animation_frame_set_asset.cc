#include "artwork/regenerate_animation_frame_set_asset.h"

#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "artwork/prepare_animation_frame_set_asset.h"
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

absl::Status RestoreBindings(const AnimationFrameSetRecipe& recipe,
                             const Blueprint& blueprint_snapshot, Blueprint* restored) {
  *restored = blueprint_snapshot;
  for (const AnimationFrameSetBlueprintBinding& binding : recipe.blueprint_bindings) {
    ASSIGN_OR_RETURN(const size_t state_index, FindState(*restored, binding.state_key));
    if (restored->states[state_index].sprite_id != recipe.sprite_id) {
      return absl::FailedPreconditionError(
          absl::StrCat("Blueprint state '", binding.state_key,
                       "' no longer has the recipe-owned animation Sprite binding"));
    }
    restored->states[state_index].sprite_id = binding.previous_sprite_id;
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<AnimationFrameSetBlueprintBinding>> ApplyBindings(
    const std::vector<std::string>& state_keys, std::string_view sprite_id, Blueprint* blueprint) {
  if (state_keys.empty()) {
    return absl::InvalidArgumentError(
        "animation frame-set regeneration must bind at least one Blueprint state");
  }
  std::set<std::string> unique_keys;
  std::vector<AnimationFrameSetBlueprintBinding> bindings;
  bindings.reserve(state_keys.size());
  for (const std::string& state_key : state_keys) {
    if (!unique_keys.insert(state_key).second) {
      return absl::InvalidArgumentError(
          "animation frame-set regeneration Blueprint state keys must be unique");
    }
    ASSIGN_OR_RETURN(const size_t state_index, FindState(*blueprint, state_key));
    bindings.push_back({
        .state_key = state_key,
        .previous_sprite_id = blueprint->states[state_index].sprite_id,
    });
    blueprint->states[state_index].sprite_id = std::string(sprite_id);
  }
  return bindings;
}

absl::Status ValidateInputGraph(const PreparedAnimationFrameSetRegeneration& prepared) {
  if (prepared.recipe_snapshot.source_artwork_id != prepared.source_snapshot.id) {
    return absl::InvalidArgumentError(
        "animation frame-set regeneration recipe names a different retained source");
  }
  if (prepared.recipe_snapshot.texture_id != prepared.texture_snapshot.id ||
      prepared.recipe_snapshot.sprite_id != prepared.sprite_snapshot.id ||
      prepared.recipe_snapshot.blueprint_id != prepared.blueprint_snapshot.id) {
    return absl::InvalidArgumentError(
        "animation frame-set regeneration snapshots do not match recipe bindings");
  }
  if (prepared.texture_snapshot.name != prepared.recipe_snapshot.name ||
      prepared.texture_snapshot.path != TexturePath(prepared.texture_snapshot.id)) {
    return absl::InvalidArgumentError(
        "animation frame-set regeneration Texture snapshot is inconsistent");
  }
  if (prepared.sprite_snapshot.name != prepared.recipe_snapshot.name ||
      prepared.sprite_snapshot.texture_id != prepared.texture_snapshot.id ||
      prepared.sprite_snapshot.playback_mode != prepared.recipe_snapshot.pipeline.playback_mode ||
      prepared.sprite_snapshot.frames != prepared.recipe_snapshot.expected_frames) {
    return absl::FailedPreconditionError(
        "animation frame-set regeneration Sprite snapshot differs from its recipe");
  }
  Blueprint restored;
  return RestoreBindings(prepared.recipe_snapshot, prepared.blueprint_snapshot, &restored);
}

absl::Status ValidateOutputGraph(const PreparedAnimationFrameSetRegeneration& prepared) {
  if (prepared.updated_recipe.id != prepared.recipe_snapshot.id ||
      prepared.updated_recipe.name != prepared.recipe_snapshot.name ||
      prepared.updated_recipe.source_artwork_id != prepared.recipe_snapshot.source_artwork_id ||
      prepared.updated_recipe.texture_id != prepared.recipe_snapshot.texture_id ||
      prepared.updated_recipe.sprite_id != prepared.recipe_snapshot.sprite_id ||
      prepared.updated_recipe.blueprint_id != prepared.recipe_snapshot.blueprint_id) {
    return absl::InvalidArgumentError(
        "animation frame-set regeneration cannot change recipe identity or output IDs");
  }
  if (prepared.updated_sprite.id != prepared.sprite_snapshot.id ||
      prepared.updated_sprite.name != prepared.sprite_snapshot.name ||
      prepared.updated_sprite.texture_id != prepared.texture_snapshot.id ||
      prepared.updated_sprite.playback_mode != prepared.updated_recipe.pipeline.playback_mode ||
      prepared.updated_sprite.playback_mode != prepared.artwork.playback_mode ||
      prepared.updated_sprite.frames != prepared.updated_recipe.expected_frames ||
      prepared.updated_sprite.frames != prepared.artwork.sprite_frames) {
    return absl::InvalidArgumentError(
        "animation frame-set regeneration output Sprite is inconsistent");
  }

  Blueprint expected;
  RETURN_IF_ERROR(
      RestoreBindings(prepared.recipe_snapshot, prepared.blueprint_snapshot, &expected));
  for (const AnimationFrameSetBlueprintBinding& binding :
       prepared.updated_recipe.blueprint_bindings) {
    ASSIGN_OR_RETURN(const size_t state_index, FindState(expected, binding.state_key));
    if (expected.states[state_index].sprite_id != binding.previous_sprite_id) {
      return absl::InvalidArgumentError(
          "animation frame-set regeneration did not retain the prior state binding");
    }
    expected.states[state_index].sprite_id = prepared.updated_recipe.sprite_id;
  }
  if (prepared.updated_blueprint != expected) {
    return absl::InvalidArgumentError(
        "animation frame-set regeneration modifies non-binding Blueprint state");
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<PreparedAnimationFrameSetRegeneration> PrepareAnimationFrameSetRegeneration(
    const SourceArtwork& source, const RgbaImage& source_pixels,
    const AnimationFrameSetRecipe& recipe, const Texture& texture, const RgbaImage& texture_pixels,
    const Sprite& sprite, const Blueprint& blueprint,
    const AnimationFrameSetRegenerationSettings& settings) {
  PreparedAnimationFrameSetRegeneration prepared{
      .source_snapshot = source,
      .recipe_snapshot = recipe,
      .texture_snapshot = texture,
      .texture_pixels_snapshot = texture_pixels,
      .sprite_snapshot = sprite,
      .blueprint_snapshot = blueprint,
  };
  RETURN_IF_ERROR(ValidateSourceArtwork(source));
  if (!std::holds_alternative<ImportedArtworkProvenance>(source.provenance)) {
    return absl::InvalidArgumentError(
        "animation frame sets require imported or manually authored retained source");
  }
  RETURN_IF_ERROR(ValidateAnimationFrameSetRecipe(recipe));
  RETURN_IF_ERROR(ValidateInputGraph(prepared));
  if (source.width != source_pixels.width || source.height != source_pixels.height) {
    return absl::FailedPreconditionError(
        "retained source dimensions changed before animation frame-set regeneration");
  }
  ASSIGN_OR_RETURN(const std::string source_digest, RgbaImageDigest(source_pixels));
  if (source_digest != source.content_digest) {
    return absl::FailedPreconditionError(
        "retained source pixels changed before animation frame-set regeneration");
  }
  ASSIGN_OR_RETURN(const std::string texture_digest, RgbaImageDigest(texture_pixels));
  if (texture_digest != recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "animation frame-set Texture pixels differ from the recipe snapshot");
  }

  ASSIGN_OR_RETURN(prepared.artwork,
                   RunAnimationFrameSetPipeline(source_pixels, settings.style, settings.pipeline));
  Blueprint restored_blueprint;
  RETURN_IF_ERROR(RestoreBindings(recipe, blueprint, &restored_blueprint));
  ASSIGN_OR_RETURN(
      std::vector<AnimationFrameSetBlueprintBinding> bindings,
      ApplyBindings(settings.blueprint_state_keys, recipe.sprite_id, &restored_blueprint));

  prepared.updated_sprite = sprite;
  prepared.updated_sprite.playback_mode = prepared.artwork.playback_mode;
  prepared.updated_sprite.frames = prepared.artwork.sprite_frames;
  prepared.updated_blueprint = std::move(restored_blueprint);
  prepared.updated_recipe = recipe;
  prepared.updated_recipe.style = settings.style;
  prepared.updated_recipe.pipeline = settings.pipeline;
  prepared.updated_recipe.blueprint_bindings = std::move(bindings);
  prepared.updated_recipe.expected_frames = prepared.artwork.sprite_frames;
  prepared.updated_recipe.final_pixel_digest = prepared.artwork.packed_digest;
  prepared.updated_recipe.pipeline_version = kAnimationFrameSetPipelineVersion;
  RETURN_IF_ERROR(ValidatePreparedAnimationFrameSetRegeneration(prepared));
  return prepared;
}

absl::Status ValidatePreparedAnimationFrameSetRegeneration(
    const PreparedAnimationFrameSetRegeneration& prepared) {
  RETURN_IF_ERROR(ValidateSourceArtwork(prepared.source_snapshot));
  if (!std::holds_alternative<ImportedArtworkProvenance>(prepared.source_snapshot.provenance)) {
    return absl::InvalidArgumentError(
        "animation frame sets require imported or manually authored retained source");
  }
  RETURN_IF_ERROR(ValidateAnimationFrameSetRecipe(prepared.recipe_snapshot));
  RETURN_IF_ERROR(ValidateAnimationFrameSetRecipe(prepared.updated_recipe));
  RETURN_IF_ERROR(ValidateAnimationFrameSetAssetName(prepared.updated_recipe.name));
  RETURN_IF_ERROR(ValidateInputGraph(prepared));
  RETURN_IF_ERROR(ValidateOutputGraph(prepared));
  if (!prepared.texture_pixels_snapshot.IsValid() || !prepared.artwork.packed_texture.IsValid()) {
    return absl::InvalidArgumentError(
        "animation frame-set regeneration has invalid Texture pixels");
  }
  if (prepared.artwork.pipeline_version != kAnimationFrameSetPipelineVersion ||
      prepared.artwork.source_digest != prepared.source_snapshot.content_digest) {
    return absl::FailedPreconditionError(
        "animation frame-set regeneration pipeline or source snapshot is stale");
  }
  ASSIGN_OR_RETURN(const std::string old_digest, RgbaImageDigest(prepared.texture_pixels_snapshot));
  if (old_digest != prepared.recipe_snapshot.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "animation frame-set regeneration Texture snapshot is stale");
  }
  ASSIGN_OR_RETURN(const std::string new_digest, RgbaImageDigest(prepared.artwork.packed_texture));
  if (new_digest != prepared.artwork.packed_digest ||
      new_digest != prepared.updated_recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "animation frame-set regeneration output digest is inconsistent");
  }
  return absl::OkStatus();
}

}  // namespace zebes
