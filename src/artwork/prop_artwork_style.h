#pragma once

#include "absl/status/status.h"
#include "terrain/terrain_palette.h"

namespace zebes {

// The resolved visual contract retained by a prop recipe. A terrain recipe may
// be edited or detached later; regeneration still consumes this exact snapshot
// until the author explicitly refreshes it.
struct PropArtworkStyle {
  int tile_size = 32;
  int pixel_block_size = 1;
  ResolvedTerrainPalette palette;
};

absl::Status ValidatePropArtworkStyle(const PropArtworkStyle& style);

}  // namespace zebes
