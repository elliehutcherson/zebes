#pragma once

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/parallax_artwork_pipeline.h"
#include "artwork/parallax_artwork_recipe.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "objects/texture.h"

namespace zebes {

struct ParallaxArtworkRegenerationSettings {
  std::optional<std::string> terrain_recipe_id;
  ParallaxArtworkStyle style;
  ParallaxArtworkPipelineConfig pipeline;
};

struct PreparedParallaxArtworkRegeneration {
  SourceArtwork source_snapshot;
  ParallaxArtworkRecipe recipe_snapshot;
  Texture texture_snapshot;
  std::string texture_pixel_digest;

  ParallaxArtworkPipelineResult artwork;
  ParallaxArtworkRecipe updated_recipe;
};

absl::StatusOr<PreparedParallaxArtworkRegeneration> PrepareParallaxArtworkRegeneration(
    const SourceArtwork& source, const RgbaImage& source_pixels,
    const ParallaxArtworkRecipe& recipe, const Texture& texture, const RgbaImage& texture_pixels,
    const ParallaxArtworkRegenerationSettings& settings);

absl::Status ValidatePreparedParallaxArtworkRegeneration(
    const PreparedParallaxArtworkRegeneration& prepared);

}  // namespace zebes
