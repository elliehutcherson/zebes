#include "artwork/regenerate_parallax_artwork_asset.h"

#include <utility>

#include "absl/status/status.h"
#include "artwork/prepare_parallax_artwork_asset.h"
#include "common/image_digest.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

absl::Status ValidateRegenerationIdentity(const PreparedParallaxArtworkRegeneration& prepared) {
  if (prepared.texture_snapshot.id != prepared.recipe_snapshot.texture_id) {
    return absl::InvalidArgumentError(
        "parallax regeneration texture snapshot does not match its recipe");
  }
  if (prepared.updated_recipe.id != prepared.recipe_snapshot.id ||
      prepared.updated_recipe.name != prepared.recipe_snapshot.name) {
    return absl::InvalidArgumentError("parallax regeneration cannot change recipe identity");
  }
  if (prepared.updated_recipe.source_artwork_id != prepared.recipe_snapshot.source_artwork_id) {
    return absl::InvalidArgumentError("parallax regeneration cannot change its source artwork");
  }
  if (prepared.updated_recipe.texture_id != prepared.recipe_snapshot.texture_id) {
    return absl::InvalidArgumentError("parallax regeneration cannot change its output texture");
  }
  return absl::OkStatus();
}

absl::Status ValidateRegenerationTexture(const PreparedParallaxArtworkRegeneration& prepared) {
  if (prepared.texture_snapshot.name != prepared.updated_recipe.name) {
    return absl::InvalidArgumentError(
        "prepared parallax regeneration texture and recipe names differ");
  }
  const std::string expected_path =
      "textures/parallax_artwork/" + prepared.texture_snapshot.id + ".png";
  if (prepared.texture_snapshot.path != expected_path) {
    return absl::InvalidArgumentError(
        "prepared parallax regeneration texture path is inconsistent");
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<PreparedParallaxArtworkRegeneration> PrepareParallaxArtworkRegeneration(
    const SourceArtwork& source, const RgbaImage& source_pixels,
    const ParallaxArtworkRecipe& recipe, const Texture& texture, const RgbaImage& texture_pixels,
    const ParallaxArtworkRegenerationSettings& settings) {
  RETURN_IF_ERROR(ValidateSourceArtwork(source));
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(recipe));
  if (source.id != recipe.source_artwork_id) {
    return absl::FailedPreconditionError(
        "parallax artwork recipe no longer names the retained source");
  }
  if (texture.id != recipe.texture_id) {
    return absl::FailedPreconditionError(
        "parallax artwork recipe no longer names the generated texture");
  }
  ASSIGN_OR_RETURN(const std::string current_texture_digest, RgbaImageDigest(texture_pixels));
  if (current_texture_digest != recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "generated parallax texture changed outside the recipe; use Save As to preserve it");
  }

  const PrepareParallaxArtworkAssetRequest request{
      .name = recipe.name,
      .terrain_recipe_id = settings.terrain_recipe_id,
      .style = settings.style,
      .pipeline = settings.pipeline,
      .ids = {.texture_id = recipe.texture_id, .recipe_id = recipe.id},
  };
  ASSIGN_OR_RETURN(PreparedParallaxArtworkAsset rebuilt,
                   PrepareParallaxArtworkAsset(source, source_pixels, request));
  PreparedParallaxArtworkRegeneration prepared{
      .source_snapshot = source,
      .recipe_snapshot = recipe,
      .texture_snapshot = texture,
      .texture_pixel_digest = current_texture_digest,
      .artwork = std::move(rebuilt.artwork),
      .updated_recipe = std::move(rebuilt.recipe),
  };
  RETURN_IF_ERROR(ValidatePreparedParallaxArtworkRegeneration(prepared));
  return prepared;
}

absl::Status ValidatePreparedParallaxArtworkRegeneration(
    const PreparedParallaxArtworkRegeneration& prepared) {
  RETURN_IF_ERROR(ValidateSourceArtwork(prepared.source_snapshot));
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(prepared.recipe_snapshot));
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(prepared.updated_recipe));
  if (!prepared.artwork.finished.IsValid()) {
    return absl::InvalidArgumentError(
        "prepared parallax regeneration has invalid finished artwork");
  }
  if (prepared.artwork.source_digest != prepared.source_snapshot.content_digest) {
    return absl::FailedPreconditionError(
        "prepared parallax regeneration source digest is inconsistent");
  }
  ASSIGN_OR_RETURN(const std::string final_digest, RgbaImageDigest(prepared.artwork.finished));
  if (final_digest != prepared.artwork.final_digest ||
      final_digest != prepared.updated_recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "prepared parallax regeneration final digest is inconsistent");
  }
  if (prepared.texture_pixel_digest != prepared.recipe_snapshot.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "prepared parallax regeneration texture snapshot is inconsistent");
  }
  RETURN_IF_ERROR(ValidateRegenerationIdentity(prepared));
  return ValidateRegenerationTexture(prepared);
}

}  // namespace zebes
