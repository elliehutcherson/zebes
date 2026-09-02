#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/animation_frame_set_pipeline.h"
#include "artwork/animation_frame_set_recipe.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "objects/blueprint.h"
#include "objects/sprite.h"
#include "objects/texture.h"

namespace zebes {

struct AnimationFrameSetAssetIds {
  std::string texture_id;
  std::string sprite_id;
  std::string recipe_id;
};

struct PrepareAnimationFrameSetAssetRequest {
  std::string name;
  AnimationFrameSetStyle style;
  AnimationFrameSetPipelineConfig pipeline;
  AnimationFrameSetAssetIds ids;
  std::vector<std::string> blueprint_state_keys;
};

// Complete worker-owned result. Preparation only reads its value inputs and
// does not access API, filesystem, resource managers, SDL, or GPU state.
struct PreparedAnimationFrameSetAsset {
  SourceArtwork source_snapshot;
  Blueprint blueprint_snapshot;
  AnimationFrameSetPipelineResult artwork;
  Texture texture;
  Sprite sprite;
  Blueprint updated_blueprint;
  AnimationFrameSetRecipe recipe;
};

absl::Status ValidateAnimationFrameSetAssetName(std::string_view name);

absl::StatusOr<PreparedAnimationFrameSetAsset> PrepareAnimationFrameSetAsset(
    const SourceArtwork& source, const RgbaImage& source_pixels,
    const Blueprint& blueprint_snapshot, const PrepareAnimationFrameSetAssetRequest& request);

absl::Status ValidatePreparedAnimationFrameSetAsset(const PreparedAnimationFrameSetAsset& prepared);

}  // namespace zebes
