#pragma once

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/animation_frame_set_pipeline.h"
#include "nlohmann/json_fwd.hpp"
#include "objects/sprite.h"

namespace zebes {

// Records one stable Blueprint-state binding replaced by a frame-set Sprite.
// The previous Sprite ID is retained so bundle deletion can restore the exact
// authored binding instead of clearing or guessing it.
struct AnimationFrameSetBlueprintBinding {
  std::string state_key;
  std::string previous_sprite_id;

  bool operator==(const AnimationFrameSetBlueprintBinding& other) const = default;
};

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
  std::vector<AnimationFrameSetBlueprintBinding> blueprint_bindings;
  std::vector<SpriteFrame> expected_frames;
  std::string final_pixel_digest;
  int pipeline_version = kAnimationFrameSetPipelineVersion;
};

inline constexpr int kAnimationFrameSetRecipeSchemaVersion = 1;

absl::Status ValidateAnimationFrameSetRecipe(const AnimationFrameSetRecipe& recipe);
nlohmann::json AnimationFrameSetRecipeToJson(const AnimationFrameSetRecipe& recipe);
absl::StatusOr<AnimationFrameSetRecipe> AnimationFrameSetRecipeFromJson(const nlohmann::json& json);

}  // namespace zebes
