#include "curation/prop_candidate.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/source_artwork_retention.h"
#include "artwork/prepare_prop_asset.h"
#include "artwork/regenerate_prop_asset.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "generation/generated_asset_candidate.h"
#include "objects/blueprint.h"

namespace zebes {
namespace {

struct LoadedPropCreationCandidate {
  GeneratedPropCreationCandidate candidate;
  RgbaImage source_pixels;
};

struct PreparedPropRegenerationCandidate {
  PropRecipe requested_recipe;
  PreparedPropRegeneration prepared;
};

absl::Status ValidateExpectedAssetId(std::string_view candidate_id,
                                     std::optional<std::string_view> expected_asset_id) {
  if (candidate_id.empty()) {
    return absl::InvalidArgumentError("prop candidate asset ID is empty");
  }
  if (expected_asset_id.has_value() && candidate_id != *expected_asset_id) {
    return absl::InvalidArgumentError(absl::StrCat("prop candidate ID '", candidate_id,
                                                   "' does not match selected ID '",
                                                   *expected_asset_id, "'"));
  }
  return absl::OkStatus();
}

absl::StatusOr<PropRecipe> ParseRecipeCandidate(std::optional<std::string_view> expected_asset_id,
                                                const nlohmann::json& candidate) {
  ASSIGN_OR_RETURN(PropRecipe recipe, PropRecipeFromJson(candidate));
  RETURN_IF_ERROR(ValidateExpectedAssetId(recipe.id, expected_asset_id));
  if (PropRecipeToJson(recipe) != candidate) {
    return absl::InvalidArgumentError(
        "prop candidate must be one exact schema-current recipe object");
  }
  return recipe;
}

absl::StatusOr<LoadedPropCreationCandidate> LoadCreationCandidate(
    const std::filesystem::path& candidate_root, std::optional<std::string_view> expected_asset_id,
    const nlohmann::json& candidate_json) {
  ASSIGN_OR_RETURN(GeneratedPropCreationCandidate candidate,
                   GeneratedPropCreationCandidateFromJson(candidate_json));
  RETURN_IF_ERROR(ValidateExpectedAssetId(candidate.asset_id, expected_asset_id));
  ASSIGN_OR_RETURN(RgbaImage source_pixels,
                   ReadGeneratedAssetSourceCandidate(candidate_root, candidate.source));
  return LoadedPropCreationCandidate{
      .candidate = std::move(candidate),
      .source_pixels = std::move(source_pixels),
  };
}

absl::StatusOr<PreparedPropRegeneration> PrepareRegeneration(Api& api,
                                                             const PropRecipe& candidate) {
  ASSIGN_OR_RETURN(PropRecipe * loaded_recipe, api.GetPropRecipe(candidate.id));
  if (loaded_recipe == nullptr) {
    return absl::FailedPreconditionError("prop recipe lookup returned null");
  }
  const PropRecipe recipe = *loaded_recipe;
  ASSIGN_OR_RETURN(SourceArtwork * loaded_source, api.GetSourceArtwork(recipe.source_artwork_id));
  ASSIGN_OR_RETURN(Texture * loaded_texture, api.GetTexture(recipe.texture_id));
  ASSIGN_OR_RETURN(Sprite * loaded_sprite, api.GetSprite(recipe.sprite_id));
  if (loaded_source == nullptr || loaded_texture == nullptr || loaded_sprite == nullptr) {
    return absl::FailedPreconditionError("prop regeneration input lookup returned null");
  }
  ASSIGN_OR_RETURN(RgbaImage source_pixels, api.ReadSourceArtworkPixels(recipe.source_artwork_id));
  ASSIGN_OR_RETURN(RgbaImage texture_pixels, api.ReadTexturePixels(recipe.texture_id));
  return PreparePropRegeneration(*loaded_source, source_pixels, recipe, *loaded_texture,
                                 texture_pixels, *loaded_sprite,
                                 PropRegenerationSettings{
                                     .terrain_recipe_id = candidate.terrain_recipe_id,
                                     .style = candidate.style,
                                     .pipeline = candidate.pipeline,
                                 });
}

absl::StatusOr<PreparedPropRegenerationCandidate> PrepareRegenerationCandidate(
    Api& api, std::optional<std::string_view> expected_asset_id,
    const nlohmann::json& candidate_json) {
  ASSIGN_OR_RETURN(PropRecipe candidate, ParseRecipeCandidate(expected_asset_id, candidate_json));
  ASSIGN_OR_RETURN(PreparedPropRegeneration prepared, PrepareRegeneration(api, candidate));
  return PreparedPropRegenerationCandidate{
      .requested_recipe = std::move(candidate),
      .prepared = std::move(prepared),
  };
}

SourceArtwork PreviewSource(const GeneratedPropCreationCandidate& candidate) {
  return SourceArtwork{
      .id = absl::StrCat(candidate.asset_id, "-source-preview"),
      .name = absl::StrCat(candidate.name, " source"),
      .source_path = candidate.source.relative_path,
      .provenance = candidate.source.provenance,
      .width = candidate.source.width,
      .height = candidate.source.height,
      .content_digest = candidate.source.content_digest,
  };
}

PreparePropAssetRequest CreationRequest(const GeneratedPropCreationCandidate& candidate) {
  return PreparePropAssetRequest{
      .name = candidate.name,
      .terrain_recipe_id = candidate.template_recipe.terrain_recipe_id,
      .style = candidate.template_recipe.style,
      .pipeline = candidate.template_recipe.pipeline,
      .ids = candidate.ids,
  };
}

}  // namespace

std::string_view PropCandidateOperationId(PropCandidateOperation operation) {
  switch (operation) {
    case PropCandidateOperation::kCreate:
      return "create";
    case PropCandidateOperation::kRegenerate:
      return "regenerate";
  }
  return "invalid";
}

absl::StatusOr<BlueprintPlacementMode> ResolvePropPlacementMode(const Blueprint& blueprint,
                                                                std::string_view sprite_id) {
  for (const Blueprint::State& state : blueprint.states) {
    if (state.sprite_id != sprite_id) continue;
    if (!IsValidBlueprintPlacementMode(state.placement_mode)) {
      return absl::FailedPreconditionError("prop blueprint placement mode is invalid");
    }
    return state.placement_mode;
  }
  return absl::FailedPreconditionError("prop blueprint has no state for its sprite");
}

absl::Status ValidatePreparedPropCandidate(const PreparedPropCandidate& prepared) {
  RETURN_IF_ERROR(ValidatePropRecipe(prepared.recipe));
  if (!prepared.texture.IsValid() || prepared.sprite.id != prepared.recipe.sprite_id ||
      prepared.sprite.texture_id != prepared.recipe.texture_id ||
      prepared.sprite.frames.size() != 1 ||
      prepared.sprite.frames.front() != prepared.recipe.expected_frame ||
      !IsValidBlueprintPlacementMode(prepared.placement_mode) ||
      !IsLowercaseSha256Digest(prepared.source_content_digest) ||
      prepared.requested_candidate.is_null() ||
      PropCandidateOperationId(prepared.operation) == "invalid") {
    return absl::FailedPreconditionError("prepared prop candidate graph is invalid");
  }
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(prepared.texture));
  if (digest != prepared.recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "prepared prop candidate pixels do not match the recipe digest");
  }
  return absl::OkStatus();
}

absl::StatusOr<PreparedPropCandidate> PreparePropCandidateForReview(
    Api& api, const std::filesystem::path& candidate_root,
    std::optional<std::string_view> expected_asset_id, const nlohmann::json& candidate_json) {
  PreparedPropCandidate result;
  if (IsGeneratedAssetCreationCandidate(candidate_json)) {
    ASSIGN_OR_RETURN(LoadedPropCreationCandidate loaded,
                     LoadCreationCandidate(candidate_root, expected_asset_id, candidate_json));
    ASSIGN_OR_RETURN(PreparedPropAsset prepared,
                     PreparePropAsset(PreviewSource(loaded.candidate), loaded.source_pixels,
                                      CreationRequest(loaded.candidate)));
    ASSIGN_OR_RETURN(const BlueprintPlacementMode placement_mode,
                     ResolvePropPlacementMode(prepared.blueprint, prepared.sprite.id));
    result = {
        .recipe = std::move(prepared.recipe),
        .texture = std::move(prepared.artwork.finished.image),
        .sprite = std::move(prepared.sprite),
        .placement_mode = placement_mode,
        .operation = PropCandidateOperation::kCreate,
        .matches_deterministic_output = true,
        .source_content_digest = loaded.candidate.source.content_digest,
        .requested_candidate = candidate_json,
    };
  } else {
    ASSIGN_OR_RETURN(PreparedPropRegenerationCandidate regeneration,
                     PrepareRegenerationCandidate(api, expected_asset_id, candidate_json));
    ASSIGN_OR_RETURN(Blueprint * blueprint,
                     api.GetBlueprint(regeneration.prepared.updated_recipe.blueprint_id));
    if (blueprint == nullptr) {
      return absl::FailedPreconditionError("prop blueprint lookup returned null");
    }
    ASSIGN_OR_RETURN(const BlueprintPlacementMode placement_mode,
                     ResolvePropPlacementMode(*blueprint, regeneration.prepared.updated_sprite.id));
    const bool matches_deterministic_output =
        candidate_json == PropRecipeToJson(regeneration.prepared.updated_recipe);
    result = {
        .recipe = std::move(regeneration.prepared.updated_recipe),
        .texture = std::move(regeneration.prepared.artwork.finished.image),
        .sprite = std::move(regeneration.prepared.updated_sprite),
        .placement_mode = placement_mode,
        .operation = PropCandidateOperation::kRegenerate,
        .matches_deterministic_output = matches_deterministic_output,
        .source_content_digest = regeneration.prepared.source_snapshot.content_digest,
        .requested_candidate = candidate_json,
    };
  }
  RETURN_IF_ERROR(ValidatePreparedPropCandidate(result));
  return result;
}

absl::Status CommitPropCandidate(Api& api, const std::filesystem::path& candidate_root,
                                 std::string_view expected_asset_id,
                                 const nlohmann::json& candidate_json) {
  if (IsGeneratedAssetCreationCandidate(candidate_json)) {
    ASSIGN_OR_RETURN(LoadedPropCreationCandidate loaded,
                     LoadCreationCandidate(candidate_root, expected_asset_id, candidate_json));
    return RetainSourceArtwork(
               api, absl::StrCat(loaded.candidate.name, " source"),
               loaded.candidate.source.provenance, loaded.source_pixels,
               [&api, &candidate = loaded.candidate](
                   const SourceArtwork& source, const RgbaImage& retained_pixels) -> absl::Status {
                 ASSIGN_OR_RETURN(
                     PreparedPropAsset prepared,
                     PreparePropAsset(source, retained_pixels, CreationRequest(candidate)));
                 return api.CreateGeneratedProp(prepared).status();
               })
        .status();
  }

  ASSIGN_OR_RETURN(PreparedPropRegenerationCandidate regeneration,
                   PrepareRegenerationCandidate(api, expected_asset_id, candidate_json));
  if (regeneration.requested_recipe.final_pixel_digest !=
      regeneration.prepared.updated_recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "prop candidate digest does not match the deterministic regenerated pixels");
  }
  if (candidate_json != PropRecipeToJson(regeneration.prepared.updated_recipe)) {
    return absl::FailedPreconditionError(
        "prop candidate changes immutable bundle identity or derived output fields");
  }
  return api.RegenerateGeneratedProp(regeneration.prepared);
}

}  // namespace zebes
