#pragma once

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/types/span.h"

namespace zebes {

// Writes tightly packed RGBA8 pixels to a PNG file, creating parent
// directories as needed.
//
// The editor needs this because artwork it generates has to become a real file
// on disk before a texture definition can point at it. Encoding lives here
// rather than behind the SDL boundary so that code with no renderer -- asset
// tools, resource managers, tests -- can write an image.
absl::Status WritePng(const std::string& path, int width, int height,
                      absl::Span<const uint8_t> pixels);

}  // namespace zebes
