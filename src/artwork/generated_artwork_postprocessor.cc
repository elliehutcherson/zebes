#include "artwork/generated_artwork_postprocessor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "artwork/prop_artwork.h"
#include "artwork/quantize_prop.h"
#include "artwork/rasterize_prop.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

constexpr int kMaximumOutputDimension = 4096;
constexpr int kMaximumPaletteColors = 256;

absl::Status ValidatePolicies(const GeneratedArtworkPostprocessConfig& config) {
  switch (config.background_policy) {
    case GeneratedArtworkBackgroundPolicy::kPreserve:
    case GeneratedArtworkBackgroundPolicy::kRemoveSolidMatte:
      break;
    default:
      return absl::InvalidArgumentError("generated artwork background policy is invalid");
  }
  switch (config.palette_policy) {
    case GeneratedArtworkPalettePolicy::kPreserve:
    case GeneratedArtworkPalettePolicy::kQuantize:
      break;
    default:
      return absl::InvalidArgumentError("generated artwork palette policy is invalid");
  }
  switch (config.alpha_policy) {
    case GeneratedArtworkAlphaPolicy::kPreserve:
    case GeneratedArtworkAlphaPolicy::kBinary:
    case GeneratedArtworkAlphaPolicy::kOpaque:
      return absl::OkStatus();
  }
  return absl::InvalidArgumentError("generated artwork alpha policy is invalid");
}

absl::Status ValidateOutputGeometry(const GeneratedArtworkPostprocessConfig& config) {
  if (config.output_width <= 0 || config.output_height <= 0) {
    return absl::InvalidArgumentError("generated artwork output dimensions must be positive");
  }
  if (config.output_width > kMaximumOutputDimension ||
      config.output_height > kMaximumOutputDimension) {
    return absl::InvalidArgumentError("generated artwork output dimensions exceed 4096 pixels");
  }
  if (config.pixel_block_size <= 0) {
    return absl::InvalidArgumentError("generated artwork pixel block size must be positive");
  }
  if (config.output_width % config.pixel_block_size != 0 ||
      config.output_height % config.pixel_block_size != 0) {
    return absl::InvalidArgumentError(
        "generated artwork output dimensions must divide into whole pixel blocks");
  }
  if (config.minimum_transparent_border < 0) {
    return absl::InvalidArgumentError(
        "generated artwork minimum transparent border cannot be negative");
  }
  const bool border_consumes_width =
      config.minimum_transparent_border >= (config.output_width + 1) / 2;
  const bool border_consumes_height =
      config.minimum_transparent_border >= (config.output_height + 1) / 2;
  if (config.alpha_policy != GeneratedArtworkAlphaPolicy::kOpaque &&
      (border_consumes_width || border_consumes_height)) {
    return absl::InvalidArgumentError(
        "generated artwork transparent border leaves no usable output area");
  }
  return absl::OkStatus();
}

absl::Status ValidateMatteSettings(const GeneratedArtworkPostprocessConfig& config) {
  if (config.background.a != 255) {
    return absl::InvalidArgumentError("generated artwork matte color must be opaque");
  }
  if (!std::isfinite(config.transparent_distance) || config.transparent_distance < 0.0f) {
    return absl::InvalidArgumentError(
        "generated artwork transparent matte distance must be finite and non-negative");
  }
  const double maximum_rgb_distance = std::sqrt(3.0 * 255.0 * 255.0);
  if (!std::isfinite(config.opaque_distance) ||
      config.opaque_distance <= config.transparent_distance ||
      config.opaque_distance > maximum_rgb_distance) {
    return absl::InvalidArgumentError(
        "generated artwork opaque matte distance must exceed the transparent distance and fit RGB");
  }
  return absl::OkStatus();
}

bool HasRequiredTransparentBorder(const GeneratedArtworkBounds& bounds, const RgbaImage& image,
                                  int minimum_border) {
  return bounds.left >= minimum_border && bounds.top >= minimum_border &&
         bounds.right <= image.width - minimum_border &&
         bounds.bottom <= image.height - minimum_border;
}

bool ProcessingRequiresPalette(const GeneratedArtworkPostprocessConfig& config) {
  return config.background_policy == GeneratedArtworkBackgroundPolicy::kRemoveSolidMatte ||
         config.palette_policy == GeneratedArtworkPalettePolicy::kQuantize;
}

absl::Status ValidateAlphaSettings(const GeneratedArtworkPostprocessConfig& config) {
  if (config.final_alpha_threshold < 1 || config.final_alpha_threshold > 255) {
    return absl::InvalidArgumentError(
        "generated artwork final alpha threshold must be between 1 and 255");
  }
  if (config.minimum_visible_pixels <= 0) {
    return absl::InvalidArgumentError(
        "generated artwork minimum visible pixel count must be positive");
  }
  return absl::OkStatus();
}

absl::Status ValidatePaletteSettings(const GeneratedArtworkPostprocessConfig& config) {
  if (config.palette_alpha_threshold < 0 || config.palette_alpha_threshold > 255) {
    return absl::InvalidArgumentError(
        "generated artwork palette alpha threshold must be between 0 and 255");
  }
  if (config.maximum_palette_colors <= 0 || config.maximum_palette_colors > kMaximumPaletteColors) {
    return absl::InvalidArgumentError(
        "generated artwork maximum palette colors must be between 1 and 256");
  }
  return absl::OkStatus();
}

uint32_t PackRgb(const RgbaColor& color) {
  return (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
}

RgbaColor UnpackRgb(uint32_t packed) {
  return {
      .r = static_cast<uint8_t>((packed >> 16) & 0xff),
      .g = static_cast<uint8_t>((packed >> 8) & 0xff),
      .b = static_cast<uint8_t>(packed & 0xff),
      .a = 255,
  };
}

double DistanceSquared(const RgbaColor& left, const RgbaColor& right) {
  const double red = static_cast<double>(left.r) - right.r;
  const double green = static_cast<double>(left.g) - right.g;
  const double blue = static_cast<double>(left.b) - right.b;
  return red * red + green * green + blue * blue;
}

struct MattePixel {
  RgbaColor color;
  double coverage = 0.0;
};

MattePixel FitPaletteMatte(RgbaColor source, RgbaColor background,
                           const std::vector<RgbaColor>& palette) {
  double best_error = std::numeric_limits<double>::infinity();
  MattePixel best;
  for (const RgbaColor& candidate : palette) {
    const double red = static_cast<double>(candidate.r) - background.r;
    const double green = static_cast<double>(candidate.g) - background.g;
    const double blue = static_cast<double>(candidate.b) - background.b;
    const double denominator = red * red + green * green + blue * blue;
    if (denominator == 0.0) continue;
    const double source_red = static_cast<double>(source.r) - background.r;
    const double source_green = static_cast<double>(source.g) - background.g;
    const double source_blue = static_cast<double>(source.b) - background.b;
    const double coverage = std::clamp(
        (source_red * red + source_green * green + source_blue * blue) / denominator, 0.0, 1.0);
    const double predicted_red = background.r + coverage * red;
    const double predicted_green = background.g + coverage * green;
    const double predicted_blue = background.b + coverage * blue;
    const double error_red = source.r - predicted_red;
    const double error_green = source.g - predicted_green;
    const double error_blue = source.b - predicted_blue;
    const double error =
        error_red * error_red + error_green * error_green + error_blue * error_blue;
    if (error < best_error) {
      best_error = error;
      best = {.color = candidate, .coverage = coverage};
    }
  }
  return best;
}

absl::StatusOr<std::pair<RgbaImage, size_t>> MatteSolidBackground(
    const RgbaImage& source, const std::vector<RgbaColor>& palette,
    const GeneratedArtworkPostprocessConfig& config) {
  RgbaImage matted = source;
  const double transparent_squared =
      static_cast<double>(config.transparent_distance) * config.transparent_distance;
  const double opaque_squared =
      static_cast<double>(config.opaque_distance) * config.opaque_distance;
  size_t partially_matted = 0;
  const size_t pixel_count = static_cast<size_t>(source.width) * source.height;
  std::vector<uint8_t> removable_matte(pixel_count, 0);
  std::queue<size_t> pending;
  const auto enqueue = [&](int x, int y) {
    const size_t pixel = static_cast<size_t>(y) * source.width + x;
    if (removable_matte[pixel] != 0) return;
    const size_t offset = pixel * 4;
    const RgbaColor color{source.pixels[offset + 0], source.pixels[offset + 1],
                          source.pixels[offset + 2], source.pixels[offset + 3]};
    if (color.a != 0 && DistanceSquared(color, config.background) >= opaque_squared) return;
    removable_matte[pixel] = 1;
    pending.push(pixel);
  };

  // Preserve the tolerant exterior behavior: a generated matte may vary at
  // the canvas edge without containing an exact configured-color pixel there.
  for (int x = 0; x < source.width; ++x) {
    enqueue(x, 0);
    enqueue(x, source.height - 1);
  }
  for (int y = 1; y + 1 < source.height; ++y) {
    enqueue(0, y);
    enqueue(source.width - 1, y);
  }

  // An opening surrounded by foreground cannot be reached from the canvas
  // edge. Seed any such component only from pixels that match the explicitly
  // configured matte closely enough to become transparent. Flooding from
  // those cores decontaminates their fringe without treating every isolated
  // foreground color inside the broader opaque distance as background.
  for (int y = 0; y < source.height; ++y) {
    for (int x = 0; x < source.width; ++x) {
      const size_t offset = (static_cast<size_t>(y) * source.width + x) * 4;
      const RgbaColor color{source.pixels[offset + 0], source.pixels[offset + 1],
                            source.pixels[offset + 2], source.pixels[offset + 3]};
      if (color.a == 0 || DistanceSquared(color, config.background) <= transparent_squared) {
        enqueue(x, y);
      }
    }
  }

  constexpr int kDx[] = {-1, 1, 0, 0};
  constexpr int kDy[] = {0, 0, -1, 1};
  while (!pending.empty()) {
    const size_t pixel = pending.front();
    pending.pop();
    const int x = static_cast<int>(pixel % source.width);
    const int y = static_cast<int>(pixel / source.width);
    for (size_t direction = 0; direction < 4; ++direction) {
      const int neighbor_x = x + kDx[direction];
      const int neighbor_y = y + kDy[direction];
      if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= source.width ||
          neighbor_y >= source.height) {
        continue;
      }
      enqueue(neighbor_x, neighbor_y);
    }
  }

  for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
    if (removable_matte[pixel] == 0) continue;
    const size_t offset = pixel * 4;
    const uint8_t source_alpha = source.pixels[offset + 3];
    if (source_alpha == 0) {
      std::fill_n(matted.pixels.begin() + static_cast<ptrdiff_t>(offset), 4, 0);
      continue;
    }
    const RgbaColor color{
        source.pixels[offset + 0],
        source.pixels[offset + 1],
        source.pixels[offset + 2],
        source_alpha,
    };
    const double distance_squared = DistanceSquared(color, config.background);
    if (distance_squared <= transparent_squared) {
      std::fill_n(matted.pixels.begin() + static_cast<ptrdiff_t>(offset), 4, 0);
      continue;
    }
    if (distance_squared >= opaque_squared) {
      continue;
    }

    const MattePixel fitted = FitPaletteMatte(color, config.background, palette);
    const int alpha = static_cast<int>(
        std::lround(static_cast<double>(source_alpha) * std::clamp(fitted.coverage, 0.0, 1.0)));
    if (alpha <= 0) {
      std::fill_n(matted.pixels.begin() + static_cast<ptrdiff_t>(offset), 4, 0);
      continue;
    }
    matted.pixels[offset + 0] = fitted.color.r;
    matted.pixels[offset + 1] = fitted.color.g;
    matted.pixels[offset + 2] = fitted.color.b;
    matted.pixels[offset + 3] = static_cast<uint8_t>(alpha);
    if (alpha < 255) ++partially_matted;
  }
  return std::pair{std::move(matted), partially_matted};
}

absl::StatusOr<GeneratedArtworkPostprocessDiagnostics> FinalizeAlpha(
    RgbaImage& image, GeneratedArtworkAlphaPolicy alpha_policy, int alpha_threshold,
    int minimum_visible_pixels, int minimum_transparent_border, int palette_colors,
    size_t partially_matted_pixels) {
  GeneratedArtworkBounds bounds{
      .left = image.width,
      .top = image.height,
      .right = 0,
      .bottom = 0,
  };
  size_t visible_pixels = 0;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
      if (alpha_policy == GeneratedArtworkAlphaPolicy::kOpaque) {
        image.pixels[offset + 3] = 255;
      } else if (alpha_policy == GeneratedArtworkAlphaPolicy::kBinary &&
                 image.pixels[offset + 3] < alpha_threshold) {
        std::fill_n(image.pixels.begin() + static_cast<ptrdiff_t>(offset), 4, 0);
        continue;
      }
      if (image.pixels[offset + 3] == 0) {
        std::fill_n(image.pixels.begin() + static_cast<ptrdiff_t>(offset), 4, 0);
        continue;
      }
      if (alpha_policy == GeneratedArtworkAlphaPolicy::kBinary) image.pixels[offset + 3] = 255;
      ++visible_pixels;
      bounds.left = std::min(bounds.left, x);
      bounds.top = std::min(bounds.top, y);
      bounds.right = std::max(bounds.right, x + 1);
      bounds.bottom = std::max(bounds.bottom, y + 1);
    }
  }
  if (visible_pixels < static_cast<size_t>(minimum_visible_pixels)) {
    return absl::FailedPreconditionError(absl::StrCat("generated artwork has only ", visible_pixels,
                                                      " visible pixels after processing"));
  }
  if (alpha_policy != GeneratedArtworkAlphaPolicy::kOpaque &&
      !HasRequiredTransparentBorder(bounds, image, minimum_transparent_border)) {
    return absl::FailedPreconditionError(
        "generated artwork does not preserve the required transparent border");
  }
  return GeneratedArtworkPostprocessDiagnostics{
      .palette_colors = palette_colors,
      .visible_pixels = visible_pixels,
      .partially_matted_pixels = partially_matted_pixels,
      .visible_bounds = bounds,
  };
}

}  // namespace

absl::Status ValidateGeneratedArtworkPostprocessConfig(
    const GeneratedArtworkPostprocessConfig& config) {
  RETURN_IF_ERROR(ValidatePolicies(config));
  RETURN_IF_ERROR(ValidateOutputGeometry(config));
  RETURN_IF_ERROR(ValidateMatteSettings(config));
  RETURN_IF_ERROR(ValidateAlphaSettings(config));
  return ValidatePaletteSettings(config);
}

absl::StatusOr<std::vector<RgbaColor>> ExtractGeneratedArtworkPalette(const RgbaImage& reference,
                                                                      int alpha_threshold,
                                                                      int maximum_colors) {
  if (!reference.IsValid()) return absl::InvalidArgumentError("palette reference is invalid");
  if (alpha_threshold < 0 || alpha_threshold > 255) {
    return absl::InvalidArgumentError(
        "palette extraction alpha threshold must be between 0 and 255");
  }
  if (maximum_colors <= 0 || maximum_colors > kMaximumPaletteColors) {
    return absl::InvalidArgumentError(
        "palette extraction maximum colors must be between 1 and 256");
  }
  std::map<uint32_t, size_t> counts;
  const size_t pixel_count = static_cast<size_t>(reference.width) * reference.height;
  for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
    const size_t offset = pixel * 4;
    if (reference.pixels[offset + 3] <= alpha_threshold) continue;
    ++counts[PackRgb({reference.pixels[offset + 0], reference.pixels[offset + 1],
                      reference.pixels[offset + 2], 255})];
  }
  if (counts.empty()) {
    return absl::FailedPreconditionError("palette reference contains no opaque colors");
  }
  if (counts.size() > static_cast<size_t>(maximum_colors)) {
    return absl::FailedPreconditionError(absl::StrCat("palette reference contains ", counts.size(),
                                                      " colors; maximum is ", maximum_colors));
  }
  std::vector<std::pair<uint32_t, size_t>> ranked(counts.begin(), counts.end());
  std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
    if (left.second != right.second) return left.second > right.second;
    return left.first < right.first;
  });
  std::vector<RgbaColor> palette;
  palette.reserve(ranked.size());
  for (const auto& [packed, unused_count] : ranked) {
    static_cast<void>(unused_count);
    palette.push_back(UnpackRgb(packed));
  }
  return palette;
}

absl::StatusOr<RgbaImage> ResizeGeneratedArtwork(const RgbaImage& source, int output_width,
                                                 int output_height, int pixel_block_size) {
  if (!source.IsValid()) return absl::InvalidArgumentError("artwork resize source is invalid");
  const PropArtwork artwork{.image = source, .anchor_x = 0, .anchor_y = 0};
  ASSIGN_OR_RETURN(const PropArtwork resized,
                   RasterizeProp(artwork, PropRasterConfig{
                                              .tile_size = 1,
                                              .canvas_tiles_wide = output_width,
                                              .canvas_tiles_high = output_height,
                                              .pixel_block_size = pixel_block_size,
                                          }));
  return resized.image;
}

absl::StatusOr<GeneratedArtworkPostprocessResult> PostprocessGeneratedArtwork(
    const RgbaImage& source, const std::vector<RgbaColor>& palette,
    const GeneratedArtworkPostprocessConfig& config) {
  if (!source.IsValid()) return absl::InvalidArgumentError("generated source image is invalid");
  RETURN_IF_ERROR(ValidateGeneratedArtworkPostprocessConfig(config));
  if (ProcessingRequiresPalette(config) && palette.empty()) {
    return absl::FailedPreconditionError("artwork processing policy requires a palette");
  }

  RgbaImage matted = source;
  size_t partially_matted_pixels = 0;
  if (config.background_policy == GeneratedArtworkBackgroundPolicy::kRemoveSolidMatte) {
    const bool has_foreground_color =
        std::any_of(palette.begin(), palette.end(), [&config](const RgbaColor& color) {
          return DistanceSquared(color, config.background) > 0.0;
        });
    if (!has_foreground_color) {
      return absl::FailedPreconditionError(
          "palette reference has no color distinct from the generated background");
    }
    ASSIGN_OR_RETURN(auto matted_result, MatteSolidBackground(source, palette, config));
    matted = std::move(matted_result.first);
    partially_matted_pixels = matted_result.second;
  }

  ASSIGN_OR_RETURN(RgbaImage resized,
                   ResizeGeneratedArtwork(matted, config.output_width, config.output_height,
                                          config.pixel_block_size));
  RgbaImage finished = resized;
  if (config.palette_policy == GeneratedArtworkPalettePolicy::kQuantize) {
    const PropPalette prop_palette{.colors = palette, .outline = palette.front()};
    const PropArtwork resized_artwork{.image = resized, .anchor_x = 0, .anchor_y = 0};
    ASSIGN_OR_RETURN(const PropArtwork quantized, QuantizeProp(resized_artwork, prop_palette));
    finished = quantized.image;
  }
  ASSIGN_OR_RETURN(const GeneratedArtworkPostprocessDiagnostics diagnostics,
                   FinalizeAlpha(finished, config.alpha_policy, config.final_alpha_threshold,
                                 config.minimum_visible_pixels, config.minimum_transparent_border,
                                 static_cast<int>(palette.size()), partially_matted_pixels));
  return GeneratedArtworkPostprocessResult{
      .matted = std::move(matted),
      .resized = std::move(resized),
      .finished = std::move(finished),
      .palette = palette,
      .diagnostics = diagnostics,
  };
}

absl::StatusOr<GeneratedArtworkPostprocessResult> PostprocessGeneratedArtwork(
    const RgbaImage& source, const RgbaImage& palette_reference,
    const GeneratedArtworkPostprocessConfig& config) {
  if (!source.IsValid()) return absl::InvalidArgumentError("generated source image is invalid");
  RETURN_IF_ERROR(ValidateGeneratedArtworkPostprocessConfig(config));
  ASSIGN_OR_RETURN(std::vector<RgbaColor> palette,
                   ExtractGeneratedArtworkPalette(palette_reference, config.palette_alpha_threshold,
                                                  config.maximum_palette_colors));
  return PostprocessGeneratedArtwork(source, palette, config);
}

}  // namespace zebes
