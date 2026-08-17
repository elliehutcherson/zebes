#pragma once

#include "absl/status/statusor.h"
#include "artwork/prop_artwork.h"
#include "common/image_io.h"
#include "terrain/terrain_style.h"

namespace zebes {

struct PropArtworkContextPreview {
  RgbaImage image;
  int anchor_x = 0;
  int anchor_y = 0;
};

// Builds a preview-only scene using the same terrain renderer that produces
// tiles. Its bounds include the complete prop texture plus one tile of framing
// space, even when the terrain scene has less room above its ground line. The
// returned pixels are never committed to the prop texture.
absl::StatusOr<PropArtworkContextPreview> BuildPropArtworkContextPreview(
    const PropArtwork& prop, const TerrainGenConfig& terrain_config);

}  // namespace zebes
