#pragma once

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/prop_artwork_pipeline.h"
#include "artwork/prop_recipe.h"
#include "artwork/source_artwork.h"
#include "objects/blueprint.h"
#include "objects/sprite.h"
#include "objects/texture.h"

namespace zebes {

// Identities are allocated before preparation, but are not published until the
// whole bundle is committed. This makes every cross-resource reference and
// durable path available for validation before the first file is written.
struct PropAssetIds {
  std::string texture_id;
  std::string sprite_id;
  std::string blueprint_id;
  std::string recipe_id;
};

struct PreparePropAssetRequest {
  std::string name;
  std::optional<std::string> terrain_recipe_id;
  PropArtworkStyle style;
  PropArtworkPipelineConfig pipeline;
  PropAssetIds ids;
};

// Complete, platform-neutral output of prop preparation. `artwork` retains the
// stage images for review; only artwork.finished.image is committed as runtime
// texture pixels.
struct PreparedPropAsset {
  SourceArtwork source;
  PropArtworkPipelineResult artwork;
  Texture texture;
  Sprite sprite;
  Blueprint blueprint;
  PropRecipe recipe;
};

// Pure over its arguments: no resource catalogue, renderer, or filesystem is
// touched. The retained source pixels are the reproducibility authority.
absl::StatusOr<PreparedPropAsset> PreparePropAsset(const SourceArtwork& source,
                                                   const RgbaImage& source_pixels,
                                                   const PreparePropAssetRequest& request);

// Rechecks the complete internal graph and pixel digests before a prepared
// bundle crosses the persistence boundary.
absl::Status ValidatePreparedPropAsset(const PreparedPropAsset& prepared);

}  // namespace zebes
