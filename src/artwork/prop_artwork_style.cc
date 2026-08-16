#include "artwork/prop_artwork_style.h"

#include "absl/status/status.h"

namespace zebes {

absl::Status ValidatePropArtworkStyle(const PropArtworkStyle& style) {
  if (style.tile_size <= 0 || style.pixel_block_size <= 0 ||
      style.tile_size % style.pixel_block_size != 0) {
    return absl::InvalidArgumentError(
        "prop artwork style needs a positive tile size and an integer pixel block size");
  }
  if (style.palette.at(TerrainPaletteRole::kEmpty).a != 0) {
    return absl::InvalidArgumentError("prop artwork style empty palette role must be transparent");
  }
  for (size_t index = 1; index < style.palette.colors.size(); ++index) {
    if (style.palette.colors[index].a != 255) {
      return absl::InvalidArgumentError("prop artwork style terrain colour roles must be opaque");
    }
  }
  if (style.palette.OpaqueColors().empty()) {
    return absl::InvalidArgumentError("prop artwork style has no opaque terrain colours");
  }
  if (style.palette.at(TerrainPaletteRole::kOutline).a != 255) {
    return absl::InvalidArgumentError("prop artwork style outline must be opaque");
  }
  return absl::OkStatus();
}

}  // namespace zebes
