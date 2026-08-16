#include "artwork/rasterize_prop.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/status.h"

namespace zebes {
namespace {

uint8_t ToByte(double value) {
  return static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

RgbaImage AreaResize(const RgbaImage& source, int output_width, int output_height) {
  RgbaImage output;
  output.width = output_width;
  output.height = output_height;
  output.pixels.resize(static_cast<size_t>(output_width) * output_height * 4);

  const double scale_x = static_cast<double>(source.width) / output_width;
  const double scale_y = static_cast<double>(source.height) / output_height;
  for (int output_y = 0; output_y < output_height; ++output_y) {
    const double source_top = output_y * scale_y;
    const double source_bottom = (output_y + 1) * scale_y;
    const int first_y = static_cast<int>(std::floor(source_top));
    const int last_y = static_cast<int>(std::ceil(source_bottom));
    for (int output_x = 0; output_x < output_width; ++output_x) {
      const double source_left = output_x * scale_x;
      const double source_right = (output_x + 1) * scale_x;
      const int first_x = static_cast<int>(std::floor(source_left));
      const int last_x = static_cast<int>(std::ceil(source_right));

      double accumulated_alpha = 0.0;
      double accumulated_red = 0.0;
      double accumulated_green = 0.0;
      double accumulated_blue = 0.0;
      double accumulated_area = 0.0;
      for (int source_y = first_y; source_y < last_y; ++source_y) {
        if (source_y < 0 || source_y >= source.height) continue;
        const double overlap_y =
            std::max(0.0, std::min(source_bottom, static_cast<double>(source_y + 1)) -
                              std::max(source_top, static_cast<double>(source_y)));
        for (int source_x = first_x; source_x < last_x; ++source_x) {
          if (source_x < 0 || source_x >= source.width) continue;
          const double overlap_x =
              std::max(0.0, std::min(source_right, static_cast<double>(source_x + 1)) -
                                std::max(source_left, static_cast<double>(source_x)));
          const double area = overlap_x * overlap_y;
          const size_t source_pixel = (static_cast<size_t>(source_y) * source.width + source_x) * 4;
          const double alpha = source.pixels[source_pixel + 3] / 255.0;
          accumulated_alpha += alpha * area;
          accumulated_red += source.pixels[source_pixel + 0] * alpha * area;
          accumulated_green += source.pixels[source_pixel + 1] * alpha * area;
          accumulated_blue += source.pixels[source_pixel + 2] * alpha * area;
          accumulated_area += area;
        }
      }

      const size_t output_pixel = (static_cast<size_t>(output_y) * output_width + output_x) * 4;
      if (accumulated_alpha > 0.0) {
        output.pixels[output_pixel + 0] = ToByte(accumulated_red / accumulated_alpha);
        output.pixels[output_pixel + 1] = ToByte(accumulated_green / accumulated_alpha);
        output.pixels[output_pixel + 2] = ToByte(accumulated_blue / accumulated_alpha);
      } else {
        output.pixels[output_pixel + 0] = 0;
        output.pixels[output_pixel + 1] = 0;
        output.pixels[output_pixel + 2] = 0;
      }
      output.pixels[output_pixel + 3] =
          accumulated_area > 0.0 ? ToByte(accumulated_alpha / accumulated_area * 255.0) : 0;
    }
  }
  return output;
}

RgbaImage ExpandNearest(const RgbaImage& logical, int scale) {
  if (scale == 1) return logical;
  RgbaImage output;
  output.width = logical.width * scale;
  output.height = logical.height * scale;
  output.pixels.resize(static_cast<size_t>(output.width) * output.height * 4);
  for (int y = 0; y < output.height; ++y) {
    for (int x = 0; x < output.width; ++x) {
      const size_t source_pixel = (static_cast<size_t>(y / scale) * logical.width + x / scale) * 4;
      const size_t output_pixel = (static_cast<size_t>(y) * output.width + x) * 4;
      std::copy_n(logical.pixels.begin() + static_cast<ptrdiff_t>(source_pixel), 4,
                  output.pixels.begin() + static_cast<ptrdiff_t>(output_pixel));
    }
  }
  return output;
}

}  // namespace

absl::StatusOr<PropArtwork> RasterizeProp(const PropArtwork& composed,
                                          const PropRasterConfig& config) {
  if (!composed.IsValid()) return absl::InvalidArgumentError("composed prop is invalid");
  if (config.tile_size <= 0 || config.canvas_tiles_wide <= 0 || config.canvas_tiles_high <= 0 ||
      config.pixel_block_size <= 0) {
    return absl::InvalidArgumentError("prop raster settings must be positive");
  }
  const int64_t output_width = static_cast<int64_t>(config.tile_size) * config.canvas_tiles_wide;
  const int64_t output_height = static_cast<int64_t>(config.tile_size) * config.canvas_tiles_high;
  if (output_width > 4096 || output_height > 4096 || output_width % config.pixel_block_size != 0 ||
      output_height % config.pixel_block_size != 0) {
    return absl::InvalidArgumentError(
        "prop output must fit 4096 pixels and divide into whole pixel blocks");
  }

  const int logical_width = static_cast<int>(output_width) / config.pixel_block_size;
  const int logical_height = static_cast<int>(output_height) / config.pixel_block_size;
  RgbaImage logical = AreaResize(composed.image, logical_width, logical_height);
  RgbaImage output = ExpandNearest(logical, config.pixel_block_size);

  PropArtwork result{
      .image = std::move(output),
      .anchor_x = std::clamp(static_cast<int>(std::lround(static_cast<double>(composed.anchor_x) *
                                                          output_width / composed.image.width)),
                             0, static_cast<int>(output_width) - 1),
      .anchor_y = std::clamp(static_cast<int>(std::lround(static_cast<double>(composed.anchor_y) *
                                                          output_height / composed.image.height)),
                             0, static_cast<int>(output_height) - 1),
  };
  return result;
}

}  // namespace zebes
