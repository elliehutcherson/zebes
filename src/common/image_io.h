#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace zebes {

// A tightly packed RGBA8 image: row-major, four bytes per pixel, no padding.
//
// This is a plain pixel buffer, not a terrain or texture concept, so it lives
// beside the encoders rather than in whichever module happened to need it
// first.
struct RgbaImage {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;

  bool IsValid() const {
    return width > 0 && height > 0 &&
           pixels.size() == static_cast<size_t>(width) * height * 4;
  }
};

// Writes tightly packed RGBA8 pixels to a PNG file, creating parent
// directories as needed.
//
// The editor needs this because artwork it generates has to become a real file
// on disk before a texture definition can point at it. Encoding lives here
// rather than behind the SDL boundary so that code with no renderer -- asset
// tools, resource managers, tests -- can write an image.
absl::Status WritePng(const std::string& path, int width, int height,
                      absl::Span<const uint8_t> pixels);

// Reads a PNG into tightly packed RGBA8, converting whatever channel count the
// file carries.
//
// Derived terrain artwork is identified by its content, and the only place that
// content lives between sessions is the atlas PNG, so reading one back is how
// the editor learns which pictures it already has. Decoding sits here beside
// the encoder, and off the SDL boundary, for the same reason writing does.
absl::StatusOr<RgbaImage> ReadPng(const std::string& path);

}  // namespace zebes
