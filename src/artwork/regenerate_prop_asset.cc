#include "artwork/regenerate_prop_asset.h"

#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "artwork/prepare_prop_asset.h"
#include "common/image_digest.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

absl::Status ValidateRegenerationInputGraph(const PreparedPropRegeneration& prepared) {
  if (prepared.texture_snapshot.id != prepared.recipe_snapshot.texture_id) {
    return absl::InvalidArgumentError(
        "prepared regeneration texture snapshot does not match its recipe");
  }
  if (prepared.sprite_snapshot.id != prepared.recipe_snapshot.sprite_id ||
      prepared.sprite_snapshot.texture_id != prepared.texture_snapshot.id) {
    return absl::InvalidArgumentError(
        "prepared regeneration sprite snapshot does not match its recipe and texture");
  }
  if (prepared.sprite_snapshot.frames.size() != 1 ||
      prepared.sprite_snapshot.frames.front() != prepared.recipe_snapshot.expected_frame) {
    return absl::InvalidArgumentError("prepared regeneration sprite snapshot frame changed");
  }
  return absl::OkStatus();
}

absl::Status ValidateRegenerationOutputSprite(const PreparedPropRegeneration& prepared) {
  if (prepared.updated_sprite.id != prepared.sprite_snapshot.id ||
      prepared.updated_sprite.name != prepared.sprite_snapshot.name) {
    return absl::InvalidArgumentError("prepared regeneration cannot change sprite identity");
  }
  if (prepared.updated_sprite.texture_id != prepared.texture_snapshot.id) {
    return absl::InvalidArgumentError(
        "prepared regeneration output sprite names a different texture");
  }
  if (prepared.updated_sprite.frames.size() != 1 ||
      prepared.updated_sprite.frames.front() != prepared.updated_recipe.expected_frame) {
    return absl::InvalidArgumentError("prepared regeneration output sprite frame is inconsistent");
  }
  return absl::OkStatus();
}

absl::Status ValidateRegenerationRecipeIdentity(const PreparedPropRegeneration& prepared) {
  if (prepared.updated_recipe.id != prepared.recipe_snapshot.id ||
      prepared.updated_recipe.name != prepared.recipe_snapshot.name) {
    return absl::InvalidArgumentError("regeneration cannot change prop recipe identity");
  }
  if (prepared.updated_recipe.source_artwork_id != prepared.recipe_snapshot.source_artwork_id) {
    return absl::InvalidArgumentError("regeneration cannot change prop source artwork");
  }
  if (prepared.updated_recipe.texture_id != prepared.recipe_snapshot.texture_id ||
      prepared.updated_recipe.sprite_id != prepared.recipe_snapshot.sprite_id ||
      prepared.updated_recipe.blueprint_id != prepared.recipe_snapshot.blueprint_id) {
    return absl::InvalidArgumentError("regeneration cannot change prop output IDs");
  }
  return absl::OkStatus();
}

absl::Status ValidateRegenerationTexture(const PreparedPropRegeneration& prepared) {
  if (prepared.texture_snapshot.name != prepared.updated_recipe.name) {
    return absl::InvalidArgumentError("prepared regeneration texture and recipe names differ");
  }
  if (prepared.texture_snapshot.path !=
      absl::StrCat("textures/props/", prepared.texture_snapshot.id, ".png")) {
    return absl::InvalidArgumentError("prepared regeneration texture path is inconsistent");
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<PreparedPropRegeneration> PreparePropRegeneration(
    const SourceArtwork& source, const RgbaImage& source_pixels, const PropRecipe& recipe,
    const Texture& texture, const RgbaImage& texture_pixels, const Sprite& sprite,
    const PropRegenerationSettings& settings) {
  RETURN_IF_ERROR(ValidateSourceArtwork(source));
  RETURN_IF_ERROR(ValidatePropRecipe(recipe));
  if (source.id != recipe.source_artwork_id) {
    return absl::FailedPreconditionError("prop recipe no longer names the retained source");
  }
  if (texture.id != recipe.texture_id) {
    return absl::FailedPreconditionError("prop recipe no longer names the generated texture");
  }
  if (sprite.id != recipe.sprite_id || sprite.texture_id != texture.id) {
    return absl::FailedPreconditionError(
        "generated sprite identity changed outside the prop recipe; use Save As");
  }
  if (sprite.frames.size() != 1 || sprite.frames.front() != recipe.expected_frame) {
    return absl::FailedPreconditionError(
        "generated sprite frame changed outside the prop recipe; use Save As");
  }
  ASSIGN_OR_RETURN(const std::string current_texture_digest, RgbaImageDigest(texture_pixels));
  if (current_texture_digest != recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "generated texture pixels changed outside the prop recipe; use Save As to preserve them");
  }

  const PreparePropAssetRequest request{
      .name = recipe.name,
      .terrain_recipe_id = settings.terrain_recipe_id,
      .style = settings.style,
      .pipeline = settings.pipeline,
      .ids =
          PropAssetIds{
              .texture_id = recipe.texture_id,
              .sprite_id = recipe.sprite_id,
              .blueprint_id = recipe.blueprint_id,
              .recipe_id = recipe.id,
          },
  };
  ASSIGN_OR_RETURN(PreparedPropAsset rebuilt, PreparePropAsset(source, source_pixels, request));

  PreparedPropRegeneration prepared{
      .source_snapshot = source,
      .recipe_snapshot = recipe,
      .texture_snapshot = texture,
      .texture_pixel_digest = current_texture_digest,
      .sprite_snapshot = sprite,
      .artwork = std::move(rebuilt.artwork),
      .updated_sprite = std::move(rebuilt.sprite),
      .updated_recipe = std::move(rebuilt.recipe),
  };
  RETURN_IF_ERROR(ValidatePreparedPropRegeneration(prepared));
  return prepared;
}

absl::Status ValidatePreparedPropRegeneration(const PreparedPropRegeneration& prepared) {
  RETURN_IF_ERROR(ValidateSourceArtwork(prepared.source_snapshot));
  RETURN_IF_ERROR(ValidatePropRecipe(prepared.recipe_snapshot));
  RETURN_IF_ERROR(ValidatePropRecipe(prepared.updated_recipe));
  if (!prepared.artwork.finished.IsValid()) {
    return absl::InvalidArgumentError("prepared regeneration has invalid finished artwork");
  }
  if (prepared.artwork.source_digest != prepared.source_snapshot.content_digest) {
    return absl::FailedPreconditionError("prepared regeneration source digest is inconsistent");
  }
  ASSIGN_OR_RETURN(const std::string final_digest,
                   RgbaImageDigest(prepared.artwork.finished.image));
  if (final_digest != prepared.updated_recipe.final_pixel_digest) {
    return absl::FailedPreconditionError("prepared regeneration final digest is inconsistent");
  }
  if (prepared.texture_pixel_digest != prepared.recipe_snapshot.final_pixel_digest) {
    return absl::FailedPreconditionError("prepared regeneration texture snapshot is inconsistent");
  }
  RETURN_IF_ERROR(ValidateRegenerationInputGraph(prepared));
  RETURN_IF_ERROR(ValidateRegenerationOutputSprite(prepared));
  RETURN_IF_ERROR(ValidateRegenerationRecipeIdentity(prepared));
  return ValidateRegenerationTexture(prepared);
}

}  // namespace zebes
