#include "artwork/edge_treatment.h"

#include <cstddef>

#include "absl/status/status.h"
#include "common/status_macros.h"

namespace zebes {

absl::Status ValidatePropEdgeConfig(const PropEdgeConfig& config) {
  if (config.width < 0 || config.width > 8) {
    return absl::InvalidArgumentError("prop edge width must be between 0 and 8");
  }
  if (config.alpha_threshold < 0 || config.alpha_threshold > 255) {
    return absl::InvalidArgumentError("prop edge alpha threshold must be between 0 and 255");
  }
  return absl::OkStatus();
}

absl::StatusOr<PropArtwork> ApplyPropEdgeTreatment(const PropArtwork& artwork,
                                                   const RgbaColor& outline,
                                                   const PropEdgeConfig& config) {
  if (!artwork.IsValid()) return absl::InvalidArgumentError("prop artwork is invalid");
  if (outline.a != 255) return absl::InvalidArgumentError("prop outline must be opaque");
  RETURN_IF_ERROR(ValidatePropEdgeConfig(config));
  if (config.width == 0) return artwork;

  PropArtwork treated = artwork;
  for (int y = 0; y < artwork.image.height; ++y) {
    for (int x = 0; x < artwork.image.width; ++x) {
      const size_t pixel = (static_cast<size_t>(y) * artwork.image.width + x) * 4;
      if (artwork.image.pixels[pixel + 3] < config.alpha_threshold) continue;

      bool boundary = false;
      for (int dy = -config.width; dy <= config.width && !boundary; ++dy) {
        for (int dx = -config.width; dx <= config.width; ++dx) {
          const int neighbor_x = x + dx;
          const int neighbor_y = y + dy;
          if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= artwork.image.width ||
              neighbor_y >= artwork.image.height) {
            boundary = true;
            break;
          }
          const size_t neighbor =
              (static_cast<size_t>(neighbor_y) * artwork.image.width + neighbor_x) * 4;
          if (artwork.image.pixels[neighbor + 3] < config.alpha_threshold) {
            boundary = true;
            break;
          }
        }
      }
      if (!boundary) continue;
      treated.image.pixels[pixel + 0] = outline.r;
      treated.image.pixels[pixel + 1] = outline.g;
      treated.image.pixels[pixel + 2] = outline.b;
    }
  }
  return treated;
}

}  // namespace zebes
