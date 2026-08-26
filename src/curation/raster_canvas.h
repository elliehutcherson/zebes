#pragma once

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"

namespace zebes {

struct RgbaColor8 {
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  uint8_t alpha = 255;
};

struct RasterSourceRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct RasterDestinationRect {
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;
};

absl::StatusOr<RgbaImage> CreateSolidRgbaImage(int width, int height, RgbaColor8 color);

absl::StatusOr<RgbaImage> CreateCheckerboardRgbaImage(int width, int height, int cell_size,
                                                      RgbaColor8 first, RgbaColor8 second);

// Nearest-neighbour source-over composition with destination clipping. This is
// deliberately a small platform-neutral raster primitive: parallax, props,
// sprites, and future curation adapters can share it without sharing domain
// layout rules.
absl::Status CompositeRgbaNearest(RgbaImage& destination, const RgbaImage& source,
                                  RasterSourceRect source_rect,
                                  RasterDestinationRect destination_rect, double opacity = 1.0);

absl::Status FillRgbaRect(RgbaImage& destination, int x, int y, int width, int height,
                          RgbaColor8 color);

}  // namespace zebes
