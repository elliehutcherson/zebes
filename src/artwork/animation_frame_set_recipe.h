#pragma once

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/animation_frame_set_pipeline.h"
#include "nlohmann/json_fwd.hpp"
#include "objects/sprite.h"

namespace zebes {

// Strict retained-source build authority for one imported or manually authored
// frame set and its Texture, Sprite, and Blueprint-state bindings.
struct AnimationFrameSetRecipe {
  std::string id;
  std::string name;
  std::string source_artwork_id;
  AnimationFrameSetStyle style;
  AnimationFrameSetPipelineConfig pipeline;
  std::string texture_id;
  std::string sprite_id;
  std::string blueprint_id;
  // Stable keys of the Blueprint states this frame set's Sprite is bound to.
  // What each state pointed at beforehand is deliberately not recorded: it is
  // only needed to undo an import that fails partway, which happens inside one
  // run, so the caller holds it in memory. See RestoreAnimationFrameSetStates.
  std::vector<std::string> blueprint_state_keys;
  std::vector<SpriteFrame> expected_frames;
  std::string final_pixel_digest;
  int pipeline_version = kAnimationFrameSetPipelineVersion;
};

inline constexpr int kAnimationFrameSetRecipeSchemaVersion = 1;

absl::Status ValidateAnimationFrameSetRecipe(const AnimationFrameSetRecipe& recipe);
nlohmann::json AnimationFrameSetRecipeToJson(const AnimationFrameSetRecipe& recipe);
absl::StatusOr<AnimationFrameSetRecipe> AnimationFrameSetRecipeFromJson(const nlohmann::json& json);

}  // namespace zebes
