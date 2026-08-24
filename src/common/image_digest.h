#pragma once

#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "common/image_io.h"

namespace zebes {

// True only for the canonical lowercase hexadecimal representation returned by
// RgbaImageDigest.
bool IsLowercaseSha256Digest(std::string_view digest);

// Returns lowercase SHA-256 over a canonical big-endian width/height header
// followed by the decoded RGBA bytes. Encoder metadata therefore cannot change
// the identity of otherwise identical source artwork.
absl::StatusOr<std::string> RgbaImageDigest(const RgbaImage& image);

}  // namespace zebes
