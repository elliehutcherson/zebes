#pragma once

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/prop_artwork_pipeline.h"
#include "artwork/prop_artwork_style.h"
#include "nlohmann/json_fwd.hpp"
#include "objects/sprite.h"

namespace zebes {

// Strict editor-only build record for one generated prop bundle. The attached
// terrain recipe is an authoring relationship; the resolved style remains the
// regeneration authority until explicitly refreshed.
struct PropRecipe {
  std::string id;
  std::string name;
  std::string source_artwork_id;
  std::optional<std::string> terrain_recipe_id;
  PropArtworkStyle style;
  PropArtworkPipelineConfig pipeline;
  std::string texture_id;
  std::string sprite_id;
  std::string blueprint_id;
  SpriteFrame expected_frame;
  std::string final_pixel_digest;
  int pipeline_version = kPropArtworkPipelineVersion;
};

inline constexpr int kPropRecipeSchemaVersion = 1;

absl::Status ValidatePropRecipe(const PropRecipe& recipe);
nlohmann::json PropRecipeToJson(const PropRecipe& recipe);
absl::StatusOr<PropRecipe> PropRecipeFromJson(const nlohmann::json& json);

}  // namespace zebes
