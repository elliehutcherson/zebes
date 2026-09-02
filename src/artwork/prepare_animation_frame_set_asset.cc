#include "artwork/prepare_animation_frame_set_asset.h"

#include <array>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/common.h"
#include "common/image_digest.h"
#include "common/resource_identity.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

absl::Status ValidateIds(const AnimationFrameSetAssetIds& ids) {
  const std::array<std::pair<std::string_view, std::string_view>, 3> named_ids = {{
      {"texture", ids.texture_id},
      {"sprite", ids.sprite_id},
      {"recipe", ids.recipe_id},
  }};
  std::set<std::string_view> distinct_ids;
  for (const auto& [kind, id] : named_ids) {
    if (!IsPathSafeResourceId(id)) {
      return absl::InvalidArgumentError(absl::StrCat(kind, " ID is not path-safe"));
    }
    if (!distinct_ids.insert(id).second) {
      return absl::InvalidArgumentError("prepared animation frame-set asset IDs must be distinct");
    }
  }
  return absl::OkStatus();
}

std::string TexturePath(std::string_view texture_id) {
  return absl::StrCat("textures/animation_frame_sets/", texture_id, ".png");
}

absl::Status ValidateBlueprintSnapshot(const Blueprint& blueprint) {
  if (blueprint.id.empty() || blueprint.name.empty()) {
    return absl::InvalidArgumentError(
        "prepared animation frame set needs an identified Blueprint snapshot");
  }
  std::set<std::string> state_keys;
  for (const Blueprint::State& state : blueprint.states) {
    if (!IsValidBlueprintStateKey(state.key) || !state_keys.insert(state.key).second) {
      return absl::InvalidArgumentError(
          "prepared animation frame set Blueprint state keys are invalid or duplicated");
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<size_t> FindState(const Blueprint& blueprint, std::string_view state_key) {
  for (size_t index = 0; index < blueprint.states.size(); ++index) {
    if (blueprint.states[index].key == state_key) return index;
  }
  return absl::NotFoundError(absl::StrCat("Blueprint has no state with key '", state_key, "'"));
}

absl::StatusOr<std::vector<AnimationFrameSetBlueprintBinding>> BuildBindings(
    const Blueprint& blueprint, const std::vector<std::string>& state_keys,
    std::string_view sprite_id, Blueprint* updated_blueprint) {
  if (state_keys.empty()) {
    return absl::InvalidArgumentError(
        "prepared animation frame set must bind at least one Blueprint state");
  }
  std::set<std::string> unique_keys;
  std::vector<AnimationFrameSetBlueprintBinding> bindings;
  bindings.reserve(state_keys.size());
  *updated_blueprint = blueprint;
  for (const std::string& state_key : state_keys) {
    if (!unique_keys.insert(state_key).second) {
      return absl::InvalidArgumentError(
          "prepared animation frame-set Blueprint state keys must be unique");
    }
    ASSIGN_OR_RETURN(const size_t state_index, FindState(blueprint, state_key));
    const std::string& previous_sprite_id = blueprint.states[state_index].sprite_id;
    if (previous_sprite_id == sprite_id) {
      return absl::InvalidArgumentError(
          "prepared animation frame-set Sprite is already bound before it exists");
    }
    bindings.push_back({
        .state_key = state_key,
        .previous_sprite_id = previous_sprite_id,
    });
    updated_blueprint->states[state_index].sprite_id = std::string(sprite_id);
  }
  return bindings;
}

absl::Status ValidateTexture(const PreparedAnimationFrameSetAsset& prepared) {
  if (prepared.texture.name != prepared.recipe.name) {
    return absl::InvalidArgumentError(
        "prepared animation frame-set Texture and recipe names differ");
  }
  if (prepared.texture.path != TexturePath(prepared.texture.id)) {
    return absl::InvalidArgumentError("prepared animation frame-set Texture path is inconsistent");
  }
  return absl::OkStatus();
}

absl::Status ValidateSprite(const PreparedAnimationFrameSetAsset& prepared) {
  if (prepared.sprite.name != prepared.recipe.name) {
    return absl::InvalidArgumentError(
        "prepared animation frame-set Sprite and recipe names differ");
  }
  if (prepared.sprite.texture_id != prepared.texture.id) {
    return absl::InvalidArgumentError(
        "prepared animation frame-set Sprite names a different Texture");
  }
  if (prepared.sprite.playback_mode != prepared.recipe.pipeline.playback_mode ||
      prepared.sprite.playback_mode != prepared.artwork.playback_mode ||
      prepared.sprite.frames != prepared.recipe.expected_frames ||
      prepared.sprite.frames != prepared.artwork.sprite_frames) {
    return absl::InvalidArgumentError(
        "prepared animation frame-set Sprite metadata is inconsistent");
  }
  return absl::OkStatus();
}

absl::Status ValidateBlueprintChange(const PreparedAnimationFrameSetAsset& prepared) {
  RETURN_IF_ERROR(ValidateBlueprintSnapshot(prepared.blueprint_snapshot));
  if (prepared.recipe.blueprint_id != prepared.blueprint_snapshot.id ||
      prepared.updated_blueprint.id != prepared.blueprint_snapshot.id) {
    return absl::InvalidArgumentError(
        "prepared animation frame-set recipe names a different Blueprint");
  }
  Blueprint expected = prepared.blueprint_snapshot;
  for (const AnimationFrameSetBlueprintBinding& binding : prepared.recipe.blueprint_bindings) {
    ASSIGN_OR_RETURN(const size_t state_index, FindState(expected, binding.state_key));
    if (expected.states[state_index].sprite_id != binding.previous_sprite_id) {
      return absl::InvalidArgumentError(
          "prepared animation frame-set binding does not retain the prior Sprite ID");
    }
    expected.states[state_index].sprite_id = prepared.sprite.id;
  }
  if (prepared.updated_blueprint != expected) {
    return absl::InvalidArgumentError(
        "prepared animation frame-set Blueprint change modifies non-binding state");
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ValidateAnimationFrameSetAssetName(std::string_view name) {
  if (name.empty()) {
    return absl::InvalidArgumentError("prepared animation frame set needs a name");
  }
  if (name.size() > kMaxTextureNameLength) {
    return absl::InvalidArgumentError(absl::StrCat(
        "prepared animation frame-set name is longer than ", kMaxTextureNameLength, " characters"));
  }
  if (!IsSafeResourceName(name)) {
    return absl::InvalidArgumentError("prepared animation frame-set name is not a safe filename");
  }
  return absl::OkStatus();
}

absl::StatusOr<PreparedAnimationFrameSetAsset> PrepareAnimationFrameSetAsset(
    const SourceArtwork& source, const RgbaImage& source_pixels,
    const Blueprint& blueprint_snapshot, const PrepareAnimationFrameSetAssetRequest& request) {
  RETURN_IF_ERROR(ValidateSourceArtwork(source));
  if (!std::holds_alternative<ImportedArtworkProvenance>(source.provenance)) {
    return absl::InvalidArgumentError(
        "animation frame sets require imported or manually authored retained source");
  }
  RETURN_IF_ERROR(ValidateBlueprintSnapshot(blueprint_snapshot));
  RETURN_IF_ERROR(ValidateAnimationFrameSetAssetName(request.name));
  RETURN_IF_ERROR(ValidateIds(request.ids));
  if (source.width != source_pixels.width || source.height != source_pixels.height) {
    return absl::FailedPreconditionError(
        "retained source dimensions do not match the animation frame-set source");
  }
  ASSIGN_OR_RETURN(const std::string source_digest, RgbaImageDigest(source_pixels));
  if (source_digest != source.content_digest) {
    return absl::FailedPreconditionError(
        "retained source pixels do not match the animation frame-set source digest");
  }

  ASSIGN_OR_RETURN(AnimationFrameSetPipelineResult artwork,
                   RunAnimationFrameSetPipeline(source_pixels, request.style, request.pipeline));
  Blueprint updated_blueprint;
  ASSIGN_OR_RETURN(std::vector<AnimationFrameSetBlueprintBinding> bindings,
                   BuildBindings(blueprint_snapshot, request.blueprint_state_keys,
                                 request.ids.sprite_id, &updated_blueprint));

  PreparedAnimationFrameSetAsset prepared{
      .source_snapshot = source,
      .blueprint_snapshot = blueprint_snapshot,
      .artwork = std::move(artwork),
      .texture =
          Texture{
              .id = request.ids.texture_id,
              .name = request.name,
              .path = TexturePath(request.ids.texture_id),
          },
      .sprite =
          Sprite{
              .id = request.ids.sprite_id,
              .name = request.name,
              .texture_id = request.ids.texture_id,
              .playback_mode = request.pipeline.playback_mode,
          },
      .updated_blueprint = std::move(updated_blueprint),
      .recipe =
          AnimationFrameSetRecipe{
              .id = request.ids.recipe_id,
              .name = request.name,
              .source_artwork_id = source.id,
              .style = request.style,
              .pipeline = request.pipeline,
              .texture_id = request.ids.texture_id,
              .sprite_id = request.ids.sprite_id,
              .blueprint_id = blueprint_snapshot.id,
              .blueprint_bindings = std::move(bindings),
              .final_pixel_digest = "",
              .pipeline_version = kAnimationFrameSetPipelineVersion,
          },
  };
  prepared.sprite.frames = prepared.artwork.sprite_frames;
  prepared.recipe.expected_frames = prepared.artwork.sprite_frames;
  prepared.recipe.final_pixel_digest = prepared.artwork.packed_digest;
  RETURN_IF_ERROR(ValidatePreparedAnimationFrameSetAsset(prepared));
  return prepared;
}

absl::Status ValidatePreparedAnimationFrameSetAsset(
    const PreparedAnimationFrameSetAsset& prepared) {
  RETURN_IF_ERROR(ValidateSourceArtwork(prepared.source_snapshot));
  if (!std::holds_alternative<ImportedArtworkProvenance>(prepared.source_snapshot.provenance)) {
    return absl::InvalidArgumentError(
        "animation frame sets require imported or manually authored retained source");
  }
  RETURN_IF_ERROR(ValidateAnimationFrameSetAssetName(prepared.recipe.name));
  RETURN_IF_ERROR(ValidateIds({
      .texture_id = prepared.texture.id,
      .sprite_id = prepared.sprite.id,
      .recipe_id = prepared.recipe.id,
  }));
  RETURN_IF_ERROR(ValidateAnimationFrameSetRecipe(prepared.recipe));
  if (!prepared.artwork.packed_texture.IsValid()) {
    return absl::InvalidArgumentError(
        "prepared animation frame set has invalid packed Texture pixels");
  }
  if (prepared.artwork.pipeline_version != kAnimationFrameSetPipelineVersion) {
    return absl::FailedPreconditionError(
        "prepared animation frame set uses an unsupported pipeline");
  }
  if (prepared.recipe.source_artwork_id != prepared.source_snapshot.id ||
      prepared.artwork.source_digest != prepared.source_snapshot.content_digest) {
    return absl::FailedPreconditionError(
        "prepared animation frame-set source changed after preparation");
  }
  ASSIGN_OR_RETURN(const std::string packed_digest,
                   RgbaImageDigest(prepared.artwork.packed_texture));
  if (packed_digest != prepared.artwork.packed_digest ||
      packed_digest != prepared.recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "prepared animation frame-set packed pixel digest does not match its recipe");
  }
  if (prepared.recipe.texture_id != prepared.texture.id ||
      prepared.recipe.sprite_id != prepared.sprite.id) {
    return absl::InvalidArgumentError(
        "prepared animation frame-set recipe output IDs are inconsistent");
  }
  RETURN_IF_ERROR(ValidateTexture(prepared));
  RETURN_IF_ERROR(ValidateSprite(prepared));
  return ValidateBlueprintChange(prepared);
}

}  // namespace zebes
