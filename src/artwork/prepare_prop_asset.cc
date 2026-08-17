#include "artwork/prepare_prop_asset.h"

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
  if (name.empty()) return absl::InvalidArgumentError("prepared prop needs a name");
  if (name.size() > kMaxTextureNameLength) {
    return absl::InvalidArgumentError(
        absl::StrCat("prepared prop name is longer than ", kMaxTextureNameLength, " characters"));
  }
  if (!IsSafeResourceName(name)) {
    return absl::InvalidArgumentError("prepared prop name is not a safe filename");
  }
  return absl::OkStatus();
}

absl::Status ValidateIds(const PropAssetIds& ids) {
  const std::array<std::pair<std::string_view, std::string_view>, 4> named_ids = {{
      {"texture", ids.texture_id},
      {"sprite", ids.sprite_id},
      {"blueprint", ids.blueprint_id},
      {"recipe", ids.recipe_id},
  }};
  for (const auto& [kind, id] : named_ids) {
    if (!IsPathSafeResourceId(id)) {
      return absl::InvalidArgumentError(absl::StrCat(kind, " ID is not path-safe"));
    }
  }
  if (ids.texture_id == ids.sprite_id || ids.texture_id == ids.blueprint_id ||
      ids.texture_id == ids.recipe_id || ids.sprite_id == ids.blueprint_id ||
      ids.sprite_id == ids.recipe_id || ids.blueprint_id == ids.recipe_id) {
    return absl::InvalidArgumentError("prepared prop asset IDs must be distinct");
  }
  return absl::OkStatus();
}

std::string PropTexturePath(std::string_view texture_id) {
  return absl::StrCat("textures/props/", texture_id, ".png");
}

}  // namespace

absl::StatusOr<PreparedPropAsset> PreparePropAsset(const SourceArtwork& source,
                                                   const RgbaImage& source_pixels,
                                                   const PreparePropAssetRequest& request) {
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

  ASSIGN_OR_RETURN(PropArtworkPipelineResult artwork,
                   RunPropArtworkPipeline(source_pixels, request.style, request.pipeline));
  const RgbaImage& finished = artwork.finished.image;
  ASSIGN_OR_RETURN(const std::string final_digest, RgbaImageDigest(finished));

  const SpriteFrame frame = {
      .index = 0,
      .texture_x = 0,
      .texture_y = 0,
      .texture_w = finished.width,
      .texture_h = finished.height,
      .render_w = finished.width,
      .render_h = finished.height,
      .frames_per_cycle = 0,
      .offset_x = -artwork.finished.anchor_x,
      .offset_y = -artwork.finished.anchor_y,
  };

  PreparedPropAsset prepared{
      .source = source,
      .artwork = std::move(artwork),
      .texture =
          Texture{
              .id = request.ids.texture_id,
              .name = request.name,
              .path = PropTexturePath(request.ids.texture_id),
          },
      .sprite =
          Sprite{
              .id = request.ids.sprite_id,
              .name = request.name,
              .texture_id = request.ids.texture_id,
              .frames = {frame},
          },
      .blueprint =
          Blueprint{
              .id = request.ids.blueprint_id,
              .name = request.name,
              .states = {Blueprint::State{
                  .name = "Default",
                  .collider_id = "",
                  .sprite_id = request.ids.sprite_id,
              }},
          },
      .recipe =
          PropRecipe{
              .id = request.ids.recipe_id,
              .name = request.name,
              .source_artwork_id = source.id,
              .terrain_recipe_id = request.terrain_recipe_id,
              .style = request.style,
              .pipeline = request.pipeline,
              .texture_id = request.ids.texture_id,
              .sprite_id = request.ids.sprite_id,
              .blueprint_id = request.ids.blueprint_id,
              .expected_frame = frame,
              .final_pixel_digest = final_digest,
              .pipeline_version = kPropArtworkPipelineVersion,
          },
  };
  RETURN_IF_ERROR(ValidatePreparedPropAsset(prepared));
  return prepared;
}

absl::Status ValidatePreparedPropAsset(const PreparedPropAsset& prepared) {
  RETURN_IF_ERROR(ValidateSourceArtwork(prepared.source));
  RETURN_IF_ERROR(ValidateName(prepared.recipe.name));
  RETURN_IF_ERROR(ValidateIds({
      .texture_id = prepared.texture.id,
      .sprite_id = prepared.sprite.id,
      .blueprint_id = prepared.blueprint.id,
      .recipe_id = prepared.recipe.id,
  }));
  RETURN_IF_ERROR(ValidatePropRecipe(prepared.recipe));
  if (!prepared.artwork.finished.IsValid()) {
    return absl::InvalidArgumentError("prepared prop has invalid finished artwork");
  }
  if (prepared.artwork.pipeline_version != kPropArtworkPipelineVersion) {
    return absl::FailedPreconditionError("prepared prop artwork uses an unsupported pipeline");
  }
  if (prepared.artwork.source_digest != prepared.source.content_digest) {
    return absl::FailedPreconditionError("prepared prop source digest changed after preparation");
  }
  ASSIGN_OR_RETURN(const std::string final_digest,
                   RgbaImageDigest(prepared.artwork.finished.image));
  if (final_digest != prepared.recipe.final_pixel_digest) {
    return absl::FailedPreconditionError("prepared prop final pixel digest does not match recipe");
  }
  if (prepared.texture.name != prepared.recipe.name ||
      prepared.texture.path != PropTexturePath(prepared.texture.id)) {
    return absl::InvalidArgumentError("prepared prop texture definition is inconsistent");
  }
  if (prepared.sprite.name != prepared.recipe.name ||
      prepared.sprite.texture_id != prepared.texture.id || prepared.sprite.frames.size() != 1 ||
      prepared.sprite.frames.front() != prepared.recipe.expected_frame) {
    return absl::InvalidArgumentError("prepared prop sprite definition is inconsistent");
  }
  const SpriteFrame& frame = prepared.sprite.frames.front();
  if (frame.offset_x != -prepared.artwork.finished.anchor_x ||
      frame.offset_y != -prepared.artwork.finished.anchor_y) {
    return absl::InvalidArgumentError("prepared prop sprite does not preserve its authored anchor");
  }
  if (prepared.blueprint.name != prepared.recipe.name || prepared.blueprint.states.size() != 1 ||
      prepared.blueprint.states.front().name.empty() ||
      !prepared.blueprint.states.front().collider_id.empty() ||
      prepared.blueprint.states.front().sprite_id != prepared.sprite.id) {
    return absl::InvalidArgumentError("prepared prop blueprint definition is inconsistent");
  }
  if (prepared.recipe.source_artwork_id != prepared.source.id ||
      prepared.recipe.texture_id != prepared.texture.id ||
      prepared.recipe.sprite_id != prepared.sprite.id ||
      prepared.recipe.blueprint_id != prepared.blueprint.id) {
    return absl::InvalidArgumentError("prepared prop recipe references are inconsistent");
  }
  return absl::OkStatus();
}

}  // namespace zebes
