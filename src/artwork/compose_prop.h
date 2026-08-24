#pragma once

#include <cstdint>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/prop_artwork.h"
#include "common/image_io.h"

namespace zebes {

enum class PropAttachmentMode : uint8_t {
  kGrounded = 0,
  kCeiling = 1,
  kFree = 2,
};

struct PropFreeAnchor {
  int x = 0;
  int y = 0;
};

struct PropAttachmentConfig {
  PropAttachmentMode mode = PropAttachmentMode::kGrounded;
  std::optional<PropFreeAnchor> free_anchor;
};

struct PropCompositionConfig {
  int canvas_tiles_wide = 3;
  int canvas_tiles_high = 2;
  float padding_fraction = 0.06f;
  PropAttachmentConfig attachment;
};

absl::Status ValidatePropCompositionConfig(const PropCompositionConfig& config);
absl::Status ValidatePropAttachment(const PropAttachmentConfig& attachment, int output_width,
                                    int output_height);

// Crops or pads around the isolated subject to the requested tile aspect and
// derives a contact anchor or maps the explicitly authored free anchor.
absl::StatusOr<PropArtwork> ComposeProp(const RgbaImage& isolated,
                                        const PropCompositionConfig& config, int final_output_width,
                                        int final_output_height);

}  // namespace zebes
