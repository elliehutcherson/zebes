#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/parallax_artwork_pipeline.h"
#include "artwork/parallax_artwork_recipe.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "objects/texture.h"

namespace zebes {

// A complete optimistic-concurrency envelope for replacing the retained
// source of an existing generated parallax asset. All resource identities are
// preserved; the source provenance/digest and derived output digest advance
// together.
struct PreparedParallaxArtworkRedraw {
  SourceArtwork source_snapshot;
  RgbaImage source_pixels_snapshot;
  SourceArtwork updated_source;
  RgbaImage updated_source_pixels;

  ParallaxArtworkRecipe recipe_snapshot;
  Texture texture_snapshot;
  std::string texture_pixel_digest;

  ParallaxArtworkPipelineResult artwork;
  ParallaxArtworkRecipe updated_recipe;
};

absl::StatusOr<PreparedParallaxArtworkRedraw> PrepareParallaxArtworkRedraw(
    const SourceArtwork& source, const RgbaImage& source_pixels,
    SourceArtworkProvenance replacement_provenance, const RgbaImage& replacement_source_pixels,
    const ParallaxArtworkRecipe& recipe, const Texture& texture, const RgbaImage& texture_pixels);

absl::Status ValidatePreparedParallaxArtworkRedraw(const PreparedParallaxArtworkRedraw& prepared);

}  // namespace zebes
