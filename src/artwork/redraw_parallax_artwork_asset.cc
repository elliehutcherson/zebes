#include "artwork/redraw_parallax_artwork_asset.h"

#include <utility>

#include "absl/status/status.h"
#include "artwork/prepare_parallax_artwork_asset.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

absl::Status ValidateIdentity(const PreparedParallaxArtworkRedraw& prepared) {
  if (prepared.updated_source.id != prepared.source_snapshot.id ||
      prepared.updated_source.name != prepared.source_snapshot.name ||
      prepared.updated_source.source_path != prepared.source_snapshot.source_path) {
    return absl::InvalidArgumentError(
        "parallax redraw cannot change retained source identity or path");
  }
  if (prepared.updated_recipe.id != prepared.recipe_snapshot.id ||
      prepared.updated_recipe.name != prepared.recipe_snapshot.name ||
      prepared.updated_recipe.source_artwork_id != prepared.recipe_snapshot.source_artwork_id ||
      prepared.updated_recipe.texture_id != prepared.recipe_snapshot.texture_id) {
    return absl::InvalidArgumentError("parallax redraw cannot change recipe bindings or identity");
  }
  if (prepared.texture_snapshot.id != prepared.recipe_snapshot.texture_id ||
      prepared.source_snapshot.id != prepared.recipe_snapshot.source_artwork_id) {
    return absl::InvalidArgumentError("parallax redraw snapshots do not match the recipe bindings");
  }

  ParallaxArtworkRecipe expected_recipe = prepared.recipe_snapshot;
  expected_recipe.final_pixel_digest = prepared.updated_recipe.final_pixel_digest;
  if (ParallaxArtworkRecipeToJson(expected_recipe) !=
      ParallaxArtworkRecipeToJson(prepared.updated_recipe)) {
    return absl::InvalidArgumentError(
        "parallax redraw may only change the recipe's final pixel digest");
  }
  return absl::OkStatus();
}

absl::Status ValidateTexture(const PreparedParallaxArtworkRedraw& prepared) {
  if (prepared.texture_snapshot.name != prepared.recipe_snapshot.name) {
    return absl::InvalidArgumentError("parallax redraw texture and recipe names differ");
  }
  const std::string expected_path =
      "textures/parallax_artwork/" + prepared.texture_snapshot.id + ".png";
  if (prepared.texture_snapshot.path != expected_path) {
    return absl::InvalidArgumentError("parallax redraw texture path is inconsistent");
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<PreparedParallaxArtworkRedraw> PrepareParallaxArtworkRedraw(
    const SourceArtwork& source, const RgbaImage& source_pixels,
    SourceArtworkProvenance replacement_provenance, const RgbaImage& replacement_source_pixels,
    const ParallaxArtworkRecipe& recipe, const Texture& texture, const RgbaImage& texture_pixels) {
  RETURN_IF_ERROR(ValidateSourceArtwork(source));
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(recipe));
  if (source.id != recipe.source_artwork_id || texture.id != recipe.texture_id) {
    return absl::FailedPreconditionError(
        "parallax artwork recipe no longer names the retained source and texture");
  }
  ASSIGN_OR_RETURN(const std::string source_digest, RgbaImageDigest(source_pixels));
  if (source.width != source_pixels.width || source.height != source_pixels.height ||
      source.content_digest != source_digest) {
    return absl::FailedPreconditionError(
        "retained source pixels no longer match the source artwork snapshot");
  }
  ASSIGN_OR_RETURN(const std::string replacement_digest,
                   RgbaImageDigest(replacement_source_pixels));
  ASSIGN_OR_RETURN(const std::string current_texture_digest, RgbaImageDigest(texture_pixels));
  if (current_texture_digest != recipe.final_pixel_digest) {
    return absl::FailedPreconditionError("generated parallax texture changed outside its recipe");
  }

  SourceArtwork updated_source = source;
  updated_source.provenance = std::move(replacement_provenance);
  updated_source.width = replacement_source_pixels.width;
  updated_source.height = replacement_source_pixels.height;
  updated_source.content_digest = replacement_digest;

  const PrepareParallaxArtworkAssetRequest request{
      .name = recipe.name,
      .terrain_recipe_id = recipe.terrain_recipe_id,
      .style = recipe.style,
      .pipeline = recipe.pipeline,
      .ids = {.texture_id = recipe.texture_id, .recipe_id = recipe.id},
  };
  ASSIGN_OR_RETURN(PreparedParallaxArtworkAsset rebuilt,
                   PrepareParallaxArtworkAsset(updated_source, replacement_source_pixels, request));
  PreparedParallaxArtworkRedraw prepared{
      .source_snapshot = source,
      .source_pixels_snapshot = source_pixels,
      .updated_source = std::move(updated_source),
      .updated_source_pixels = replacement_source_pixels,
      .recipe_snapshot = recipe,
      .texture_snapshot = texture,
      .texture_pixel_digest = current_texture_digest,
      .artwork = std::move(rebuilt.artwork),
      .updated_recipe = std::move(rebuilt.recipe),
  };
  RETURN_IF_ERROR(ValidatePreparedParallaxArtworkRedraw(prepared));
  return prepared;
}

absl::Status ValidatePreparedParallaxArtworkRedraw(const PreparedParallaxArtworkRedraw& prepared) {
  RETURN_IF_ERROR(ValidateSourceArtwork(prepared.source_snapshot));
  RETURN_IF_ERROR(ValidateSourceArtwork(prepared.updated_source));
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(prepared.recipe_snapshot));
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(prepared.updated_recipe));
  if (!prepared.source_pixels_snapshot.IsValid() || !prepared.updated_source_pixels.IsValid() ||
      !prepared.artwork.finished.IsValid()) {
    return absl::InvalidArgumentError("prepared parallax redraw contains invalid pixels");
  }

  ASSIGN_OR_RETURN(const std::string old_source_digest,
                   RgbaImageDigest(prepared.source_pixels_snapshot));
  if (prepared.source_snapshot.width != prepared.source_pixels_snapshot.width ||
      prepared.source_snapshot.height != prepared.source_pixels_snapshot.height ||
      old_source_digest != prepared.source_snapshot.content_digest) {
    return absl::FailedPreconditionError(
        "prepared parallax redraw source snapshot is inconsistent");
  }
  ASSIGN_OR_RETURN(const std::string new_source_digest,
                   RgbaImageDigest(prepared.updated_source_pixels));
  if (prepared.updated_source.width != prepared.updated_source_pixels.width ||
      prepared.updated_source.height != prepared.updated_source_pixels.height ||
      new_source_digest != prepared.updated_source.content_digest ||
      prepared.artwork.source_digest != new_source_digest) {
    return absl::FailedPreconditionError(
        "prepared parallax redraw replacement source is inconsistent");
  }
  ASSIGN_OR_RETURN(const std::string final_digest, RgbaImageDigest(prepared.artwork.finished));
  if (final_digest != prepared.artwork.final_digest ||
      final_digest != prepared.updated_recipe.final_pixel_digest) {
    return absl::FailedPreconditionError("prepared parallax redraw output digest is inconsistent");
  }
  if (prepared.texture_pixel_digest != prepared.recipe_snapshot.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "prepared parallax redraw texture snapshot is inconsistent");
  }
  RETURN_IF_ERROR(ValidateIdentity(prepared));
  return ValidateTexture(prepared);
}

}  // namespace zebes
