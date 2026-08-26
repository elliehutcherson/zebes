#include "curation/raster_canvas.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace zebes {
namespace {

constexpr int64_t kMaximumCanvasPixels = 64 * 1024 * 1024;

absl::Status ValidateDimensions(int width, int height) {
  const int64_t pixels = static_cast<int64_t>(width) * height;
  if (width <= 0 || height <= 0 || pixels > kMaximumCanvasPixels) {
    return absl::InvalidArgumentError(absl::StrCat("RGBA canvas dimensions are invalid or exceed ",
                                                   kMaximumCanvasPixels, " pixels"));
  }
  return absl::OkStatus();
}

void StoreColor(RgbaImage& image, size_t offset, RgbaColor8 color) {
  image.pixels[offset] = color.red;
  image.pixels[offset + 1] = color.green;
  image.pixels[offset + 2] = color.blue;
  image.pixels[offset + 3] = color.alpha;
}

void CompositePixel(uint8_t* destination, const uint8_t* source, int opacity) {
  const int source_alpha = (static_cast<int>(source[3]) * opacity + 127) / 255;
  const int destination_alpha = destination[3];
  const int inverse_source_alpha = 255 - source_alpha;
  const int output_alpha = source_alpha + (destination_alpha * inverse_source_alpha + 127) / 255;
  if (output_alpha == 0) {
    std::fill_n(destination, 4, 0);
    return;
  }

  for (int channel = 0; channel < 3; ++channel) {
    const int source_premultiplied = source[channel] * source_alpha;
    const int destination_premultiplied =
        (destination[channel] * destination_alpha * inverse_source_alpha + 127) / 255;
    destination[channel] = static_cast<uint8_t>(std::clamp(
        (source_premultiplied + destination_premultiplied + output_alpha / 2) / output_alpha, 0,
        255));
  }
  destination[3] = static_cast<uint8_t>(output_alpha);
}

}  // namespace

absl::StatusOr<RgbaImage> CreateSolidRgbaImage(int width, int height, RgbaColor8 color) {
  const absl::Status dimensions = ValidateDimensions(width, height);
  if (!dimensions.ok()) return dimensions;

  RgbaImage result{
      .width = width,
      .height = height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4),
  };
  for (size_t offset = 0; offset < result.pixels.size(); offset += 4) {
    StoreColor(result, offset, color);
  }
  return result;
}

absl::StatusOr<RgbaImage> CreateCheckerboardRgbaImage(int width, int height, int cell_size,
                                                      RgbaColor8 first, RgbaColor8 second) {
  if (cell_size <= 0) {
    return absl::InvalidArgumentError("checkerboard cell size must be positive");
  }
  auto result = CreateSolidRgbaImage(width, height, first);
  if (!result.ok()) return result.status();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (((x / cell_size) + (y / cell_size)) % 2 == 0) continue;
      StoreColor(*result, (static_cast<size_t>(y) * width + x) * 4, second);
    }
  }
  return result;
}

absl::Status CompositeRgbaNearest(RgbaImage& destination, const RgbaImage& source,
                                  RasterSourceRect source_rect,
                                  RasterDestinationRect destination_rect, double opacity) {
  if (!destination.IsValid() || !source.IsValid()) {
    return absl::InvalidArgumentError("RGBA composition requires valid images");
  }
  const int64_t source_right = static_cast<int64_t>(source_rect.x) + source_rect.width;
  const int64_t source_bottom = static_cast<int64_t>(source_rect.y) + source_rect.height;
  if (source_rect.x < 0 || source_rect.y < 0 || source_rect.width <= 0 || source_rect.height <= 0 ||
      source_right > source.width || source_bottom > source.height) {
    return absl::InvalidArgumentError("RGBA composition source rectangle is invalid");
  }
  if (!std::isfinite(destination_rect.x) || !std::isfinite(destination_rect.y) ||
      !std::isfinite(destination_rect.width) || !std::isfinite(destination_rect.height) ||
      destination_rect.width <= 0.0 || destination_rect.height <= 0.0 || !std::isfinite(opacity) ||
      opacity < 0.0 || opacity > 1.0) {
    return absl::InvalidArgumentError("RGBA composition destination or opacity is invalid");
  }

  const double right = destination_rect.x + destination_rect.width;
  const double bottom = destination_rect.y + destination_rect.height;
  if (!std::isfinite(right) || !std::isfinite(bottom)) {
    return absl::InvalidArgumentError("RGBA composition destination exceeds supported range");
  }
  const int first_x = static_cast<int>(std::max(
      0.0, std::min(static_cast<double>(destination.width), std::floor(destination_rect.x))));
  const int first_y = static_cast<int>(std::max(
      0.0, std::min(static_cast<double>(destination.height), std::floor(destination_rect.y))));
  const int last_x = static_cast<int>(
      std::max(0.0, std::min(static_cast<double>(destination.width), std::ceil(right))));
  const int last_y = static_cast<int>(
      std::max(0.0, std::min(static_cast<double>(destination.height), std::ceil(bottom))));
  if (last_x <= first_x || last_y <= first_y || opacity == 0.0) return absl::OkStatus();

  const int integer_opacity = static_cast<int>(std::lround(opacity * 255.0));
  for (int y = first_y; y < last_y; ++y) {
    const double source_v = std::clamp((y + 0.5 - destination_rect.y) / destination_rect.height,
                                       0.0, std::nextafter(1.0, 0.0));
    const int source_y =
        source_rect.y + static_cast<int>(source_v * static_cast<double>(source_rect.height));
    for (int x = first_x; x < last_x; ++x) {
      const double source_u = std::clamp((x + 0.5 - destination_rect.x) / destination_rect.width,
                                         0.0, std::nextafter(1.0, 0.0));
      const int source_x =
          source_rect.x + static_cast<int>(source_u * static_cast<double>(source_rect.width));
      const size_t source_offset = (static_cast<size_t>(source_y) * source.width + source_x) * 4;
      const size_t destination_offset = (static_cast<size_t>(y) * destination.width + x) * 4;
      CompositePixel(destination.pixels.data() + destination_offset,
                     source.pixels.data() + source_offset, integer_opacity);
    }
  }
  return absl::OkStatus();
}

absl::Status FillRgbaRect(RgbaImage& destination, int x, int y, int width, int height,
                          RgbaColor8 color) {
  if (!destination.IsValid() || width <= 0 || height <= 0) {
    return absl::InvalidArgumentError("RGBA fill requires a valid image and positive rectangle");
  }
  const int64_t right = static_cast<int64_t>(x) + width;
  const int64_t bottom = static_cast<int64_t>(y) + height;
  const int first_x = static_cast<int>(std::clamp<int64_t>(x, 0, destination.width));
  const int first_y = static_cast<int>(std::clamp<int64_t>(y, 0, destination.height));
  const int last_x = static_cast<int>(std::clamp<int64_t>(right, 0, destination.width));
  const int last_y = static_cast<int>(std::clamp<int64_t>(bottom, 0, destination.height));
  for (int output_y = first_y; output_y < last_y; ++output_y) {
    for (int output_x = first_x; output_x < last_x; ++output_x) {
      StoreColor(destination, (static_cast<size_t>(output_y) * destination.width + output_x) * 4,
                 color);
    }
  }
  return absl::OkStatus();
}

}  // namespace zebes
