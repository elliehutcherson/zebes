#pragma once

#include "absl/status/statusor.h"
#include "artwork/compose_prop.h"
#include "artwork/prop_artwork.h"
#include "common/image_io.h"
#include "terrain/terrain_style.h"

namespace zebes {

struct PropArtworkContextPreview {
  RgbaImage image;
  // The checkerboard and terrain before the prop is composited. Retaining this
  // preview-only layer makes repositioning cheap and never mutates source or
  // finished artwork.
  RgbaImage base_image;
  RgbaImage terrain;
  int terrain_left = 0;
  int terrain_top = 0;
  int prop_left = 0;
  int prop_top = 0;
  int anchor_x = 0;
  int anchor_y = 0;
  PropAttachmentMode attachment_mode = PropAttachmentMode::kGrounded;
};

// Builds a preview-only scene using the same terrain renderer that produces
// tiles. Its bounds include the complete prop texture plus one tile of framing
// space, even when the terrain scene has less room above its ground line. The
// returned pixels are never committed to the prop texture.
absl::StatusOr<PropArtworkContextPreview> BuildPropArtworkContextPreview(
    const PropArtwork& prop, const TerrainGenConfig& terrain_config,
    PropAttachmentMode attachment_mode);

// Repositions the prop inside an existing preview. Grounded and ceiling props
// follow the nearest valid terrain surface; free props follow both requested
// coordinates. The complete prop remains inside the fixed preview bounds.
absl::Status MovePropArtworkContextPreview(const PropArtwork& prop, int requested_anchor_x,
                                           int requested_anchor_y,
                                           PropArtworkContextPreview* preview);

}  // namespace zebes
