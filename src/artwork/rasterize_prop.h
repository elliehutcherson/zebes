#pragma once

#include "absl/status/statusor.h"
#include "artwork/prop_artwork.h"

namespace zebes {

struct PropRasterConfig {
  int tile_size = 32;
  int canvas_tiles_wide = 3;
  int canvas_tiles_high = 2;
  int pixel_block_size = 1;
};

// Area-downsamples premultiplied RGBA to the logical grid, then expands by an
// integer nearest-neighbor scale when the style uses larger pixel blocks.
absl::StatusOr<PropArtwork> RasterizeProp(const PropArtwork& composed,
                                          const PropRasterConfig& config);

}  // namespace zebes
