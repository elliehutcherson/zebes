#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/parallax_artwork_pipeline.h"
#include "artwork/parallax_artwork_recipe.h"
#include "artwork/source_artwork.h"
#include "objects/texture.h"

namespace zebes {

struct ParallaxArtworkAssetIds {
  std::string texture_id;
  std::string recipe_id;
};

struct PrepareParallaxArtworkAssetRequest {
  std::string name;
  std::optional<std::string> terrain_recipe_id;
  ParallaxArtworkStyle style;
  ParallaxArtworkPipelineConfig pipeline;
  ParallaxArtworkAssetIds ids;
};

struct PreparedParallaxArtworkAsset {
  SourceArtwork source;
  ParallaxArtworkPipelineResult artwork;
  Texture texture;
  ParallaxArtworkRecipe recipe;
};

absl::Status ValidateParallaxArtworkAssetName(std::string_view name);

absl::StatusOr<PreparedParallaxArtworkAsset> PrepareParallaxArtworkAsset(
    const SourceArtwork& source, const RgbaImage& source_pixels,
    const PrepareParallaxArtworkAssetRequest& request);

absl::Status ValidatePreparedParallaxArtworkAsset(const PreparedParallaxArtworkAsset& prepared);

}  // namespace zebes
