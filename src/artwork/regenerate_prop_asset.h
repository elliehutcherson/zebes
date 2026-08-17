#pragma once

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/prop_artwork_pipeline.h"
#include "artwork/prop_artwork_style.h"
#include "artwork/prop_recipe.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "objects/sprite.h"
#include "objects/texture.h"

namespace zebes {

struct PropRegenerationSettings {
  std::optional<std::string> terrain_recipe_id;
  PropArtworkStyle style;
  PropArtworkPipelineConfig pipeline;
};

// Worker-produced redraw plus the exact live inputs it was based on. The
// blueprint is deliberately absent: regeneration verifies that binding still
// exists but never snapshots or overwrites user-authored blueprint states and
// colliders.
struct PreparedPropRegeneration {
  SourceArtwork source_snapshot;
  PropRecipe recipe_snapshot;
  Texture texture_snapshot;
  std::string texture_pixel_digest;
  Sprite sprite_snapshot;

  PropArtworkPipelineResult artwork;
  Sprite updated_sprite;
  PropRecipe updated_recipe;
};

// Reprocesses retained source pixels without touching managers or the
// filesystem. Existing runtime IDs and the prop name are structural and cannot
// change during regeneration.
absl::StatusOr<PreparedPropRegeneration> PreparePropRegeneration(
    const SourceArtwork& source, const RgbaImage& source_pixels, const PropRecipe& recipe,
    const Texture& texture, const RgbaImage& texture_pixels, const Sprite& sprite,
    const PropRegenerationSettings& settings);

absl::Status ValidatePreparedPropRegeneration(const PreparedPropRegeneration& prepared);

}  // namespace zebes
