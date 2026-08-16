#pragma once

#include <string>

#include "absl/status/statusor.h"
#include "common/image_io.h"

namespace zebes {

// Returns lowercase SHA-256 over a canonical big-endian width/height header
// followed by the decoded RGBA bytes. Encoder metadata therefore cannot change
// the identity of otherwise identical source artwork.
absl::StatusOr<std::string> RgbaImageDigest(const RgbaImage& image);

}  // namespace zebes
