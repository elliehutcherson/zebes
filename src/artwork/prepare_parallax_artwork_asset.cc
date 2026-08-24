#include "artwork/prepare_parallax_artwork_asset.h"

#include <array>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/common.h"
#include "common/image_digest.h"
#include "common/resource_identity.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

absl::Status ValidateName(std::string_view name) {
  if (name.empty()) return absl::InvalidArgumentError("prepared parallax artwork needs a name");
  if (name.size() > kMaxTextureNameLength) {
    return absl::InvalidArgumentError(absl::StrCat("prepared parallax artwork name is longer than ",
                                                   kMaxTextureNameLength, " characters"));
  }
  if (!IsSafeResourceName(name)) {
    return absl::InvalidArgumentError("prepared parallax artwork name is not a safe filename");
  }
  return absl::OkStatus();
}

absl::Status ValidateIds(const ParallaxArtworkAssetIds& ids) {
  const std::array<std::pair<std::string_view, std::string_view>, 2> named_ids = {{
      {"texture", ids.texture_id},
      {"recipe", ids.recipe_id},
  }};
  for (const auto& [kind, id] : named_ids) {
    if (!IsPathSafeResourceId(id)) {
      return absl::InvalidArgumentError(absl::StrCat(kind, " ID is not path-safe"));
    }
  }
  if (ids.texture_id == ids.recipe_id) {
    return absl::InvalidArgumentError("prepared parallax artwork asset IDs must be distinct");
  }
  return absl::OkStatus();
}

std::string TexturePath(std::string_view texture_id) {
  return absl::StrCat("textures/parallax_artwork/", texture_id, ".png");
}

absl::Status ValidatePreparedTexture(const PreparedParallaxArtworkAsset& prepared) {
  if (prepared.texture.name != prepared.recipe.name) {
    return absl::InvalidArgumentError("prepared parallax artwork texture and recipe names differ");
  }
  if (prepared.texture.path != TexturePath(prepared.texture.id)) {
    return absl::InvalidArgumentError("prepared parallax artwork texture path is inconsistent");
  }
  return absl::OkStatus();
}

absl::Status ValidatePreparedRecipeBindings(const PreparedParallaxArtworkAsset& prepared) {
  if (prepared.recipe.source_artwork_id != prepared.source.id) {
    return absl::InvalidArgumentError("prepared parallax artwork recipe names a different source");
  }
  if (prepared.recipe.texture_id != prepared.texture.id) {
    return absl::InvalidArgumentError("prepared parallax artwork recipe names a different texture");
  }
  if (prepared.recipe.expected_width != prepared.artwork.finished.width ||
      prepared.recipe.expected_height != prepared.artwork.finished.height) {
    return absl::InvalidArgumentError(
        "prepared parallax artwork recipe dimensions differ from the finished image");
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<PreparedParallaxArtworkAsset> PrepareParallaxArtworkAsset(
    const SourceArtwork& source, const RgbaImage& source_pixels,
    const PrepareParallaxArtworkAssetRequest& request) {
  RETURN_IF_ERROR(ValidateSourceArtwork(source));
  RETURN_IF_ERROR(ValidateName(request.name));
  RETURN_IF_ERROR(ValidateIds(request.ids));
  if (source.width != source_pixels.width || source.height != source_pixels.height) {
    return absl::FailedPreconditionError(
        "retained source dimensions do not match the accepted source artwork");
  }
  ASSIGN_OR_RETURN(const std::string source_digest, RgbaImageDigest(source_pixels));
  if (source_digest != source.content_digest) {
    return absl::FailedPreconditionError(
        "retained source pixels do not match the accepted source artwork digest");
  }

  ASSIGN_OR_RETURN(ParallaxArtworkPipelineResult artwork,
                   RunParallaxArtworkPipeline(source_pixels, request.style, request.pipeline));
  const std::string final_digest = artwork.final_digest;
  PreparedParallaxArtworkAsset prepared{
      .source = source,
      .artwork = std::move(artwork),
      .texture =
          Texture{
              .id = request.ids.texture_id,
              .name = request.name,
              .path = TexturePath(request.ids.texture_id),
          },
      .recipe =
          ParallaxArtworkRecipe{
              .id = request.ids.recipe_id,
              .name = request.name,
              .source_artwork_id = source.id,
              .terrain_recipe_id = request.terrain_recipe_id,
              .style = request.style,
              .pipeline = request.pipeline,
              .texture_id = request.ids.texture_id,
              .expected_width = request.pipeline.target_width,
              .expected_height = request.pipeline.target_height,
              .final_pixel_digest = final_digest,
              .pipeline_version = kParallaxArtworkPipelineVersion,
          },
  };
  RETURN_IF_ERROR(ValidatePreparedParallaxArtworkAsset(prepared));
  return prepared;
}

absl::Status ValidatePreparedParallaxArtworkAsset(const PreparedParallaxArtworkAsset& prepared) {
  RETURN_IF_ERROR(ValidateSourceArtwork(prepared.source));
  RETURN_IF_ERROR(ValidateName(prepared.recipe.name));
  RETURN_IF_ERROR(ValidateIds({
      .texture_id = prepared.texture.id,
      .recipe_id = prepared.recipe.id,
  }));
  RETURN_IF_ERROR(ValidateParallaxArtworkRecipe(prepared.recipe));
  if (!prepared.artwork.finished.IsValid()) {
    return absl::InvalidArgumentError("prepared parallax artwork has invalid finished pixels");
  }
  if (prepared.artwork.pipeline_version != kParallaxArtworkPipelineVersion) {
    return absl::FailedPreconditionError("prepared parallax artwork uses an unsupported pipeline");
  }
  if (prepared.artwork.source_digest != prepared.source.content_digest) {
    return absl::FailedPreconditionError(
        "prepared parallax artwork source digest changed after preparation");
  }
  ASSIGN_OR_RETURN(const std::string final_digest, RgbaImageDigest(prepared.artwork.finished));
  if (final_digest != prepared.artwork.final_digest ||
      final_digest != prepared.recipe.final_pixel_digest) {
    return absl::FailedPreconditionError(
        "prepared parallax artwork final pixel digest does not match recipe");
  }
  RETURN_IF_ERROR(ValidatePreparedTexture(prepared));
  return ValidatePreparedRecipeBindings(prepared);
}

}  // namespace zebes
