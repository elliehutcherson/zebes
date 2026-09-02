#pragma once

#include <string>
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

struct AnimationFrameSetRegenerationSettings {
  AnimationFrameSetStyle style;
  AnimationFrameSetPipelineConfig pipeline;
  std::vector<std::string> blueprint_state_keys;
};

struct PreparedAnimationFrameSetRegeneration {
  SourceArtwork source_snapshot;
  AnimationFrameSetRecipe recipe_snapshot;
  Texture texture_snapshot;
  RgbaImage texture_pixels_snapshot;
  Sprite sprite_snapshot;
  Blueprint blueprint_snapshot;

  AnimationFrameSetPipelineResult artwork;
  Sprite updated_sprite;
  Blueprint updated_blueprint;
  AnimationFrameSetRecipe updated_recipe;
};

absl::StatusOr<PreparedAnimationFrameSetRegeneration> PrepareAnimationFrameSetRegeneration(
    const SourceArtwork& source, const RgbaImage& source_pixels,
    const AnimationFrameSetRecipe& recipe, const Texture& texture, const RgbaImage& texture_pixels,
    const Sprite& sprite, const Blueprint& blueprint,
    const AnimationFrameSetRegenerationSettings& settings);

absl::Status ValidatePreparedAnimationFrameSetRegeneration(
    const PreparedAnimationFrameSetRegeneration& prepared);

}  // namespace zebes
