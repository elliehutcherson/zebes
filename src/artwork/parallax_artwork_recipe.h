#pragma once

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/parallax_artwork_pipeline.h"
#include "nlohmann/json_fwd.hpp"

namespace zebes {

// Strict editor-only build authority for one processed parallax Texture. Theme
// composition remains separate and references only texture_id.
struct ParallaxArtworkRecipe {
  std::string id;
  std::string name;
  std::string source_artwork_id;
  std::optional<std::string> terrain_recipe_id;
  ParallaxArtworkStyle style;
  ParallaxArtworkPipelineConfig pipeline;
  std::string texture_id;
  int expected_width = 0;
  int expected_height = 0;
  std::string final_pixel_digest;
  int pipeline_version = kParallaxArtworkPipelineVersion;
};

inline constexpr int kParallaxArtworkRecipeSchemaVersion = 1;

absl::Status ValidateParallaxArtworkRecipe(const ParallaxArtworkRecipe& recipe);
nlohmann::json ParallaxArtworkRecipeToJson(const ParallaxArtworkRecipe& recipe);
absl::StatusOr<ParallaxArtworkRecipe> ParallaxArtworkRecipeFromJson(const nlohmann::json& json);

}  // namespace zebes
