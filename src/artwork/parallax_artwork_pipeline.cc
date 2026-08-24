#include "artwork/parallax_artwork_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "artwork/generated_artwork_postprocessor.h"
#include "common/image_digest.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

size_t PixelOffset(const RgbaImage& image, int x, int y) {
  return (static_cast<size_t>(y) * image.width + x) * 4;
}

absl::Status ValidateCropBounds(const RgbaImage& source, int left, int top, int width, int height) {
  if (left < 0 || top < 0) {
    return absl::InvalidArgumentError("parallax artwork crop origin cannot be negative");
  }
  if (width <= 0 || height <= 0) {
    return absl::InvalidArgumentError("parallax artwork crop dimensions must be positive");
  }
  if (left > source.width - width || top > source.height - height) {
    return absl::InvalidArgumentError("parallax artwork crop is outside the source image");
  }
  return absl::OkStatus();
}

absl::StatusOr<RgbaImage> Crop(const RgbaImage& source, int left, int top, int width, int height) {
  RETURN_IF_ERROR(ValidateCropBounds(source, left, top, width, height));
  RgbaImage cropped{
      .width = width,
      .height = height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4),
  };
  const size_t row_bytes = static_cast<size_t>(width) * 4;
  for (int y = 0; y < height; ++y) {
    const size_t source_offset = PixelOffset(source, left, top + y);
    const size_t target_offset = static_cast<size_t>(y) * row_bytes;
    std::copy_n(source.pixels.begin() + static_cast<ptrdiff_t>(source_offset), row_bytes,
                cropped.pixels.begin() + static_cast<ptrdiff_t>(target_offset));
  }
  return cropped;
}

absl::StatusOr<RgbaImage> CropToFill(const RgbaImage& source, int target_width, int target_height) {
  int crop_width = source.width;
  int crop_height = source.height;
  if (static_cast<int64_t>(source.width) * target_height >
      static_cast<int64_t>(source.height) * target_width) {
    crop_width = std::max(
        1, static_cast<int>(static_cast<int64_t>(source.height) * target_width / target_height));
  } else {
    crop_height = std::max(
        1, static_cast<int>(static_cast<int64_t>(source.width) * target_height / target_width));
  }
  ASSIGN_OR_RETURN(RgbaImage cropped,
                   Crop(source, (source.width - crop_width) / 2, (source.height - crop_height) / 2,
                        crop_width, crop_height));
  return ResizeGeneratedArtwork(cropped, target_width, target_height);
}

absl::StatusOr<RgbaImage> FitInside(const RgbaImage& source, int target_width, int target_height) {
  int fitted_width = target_width;
  int fitted_height = target_height;
  if (static_cast<int64_t>(source.width) * target_height >
      static_cast<int64_t>(source.height) * target_width) {
    fitted_height = std::max(
        1, static_cast<int>(static_cast<int64_t>(source.height) * target_width / source.width));
  } else {
    fitted_width = std::max(
        1, static_cast<int>(static_cast<int64_t>(source.width) * target_height / source.height));
  }
  ASSIGN_OR_RETURN(RgbaImage fitted, ResizeGeneratedArtwork(source, fitted_width, fitted_height));
  RgbaImage framed{
      .width = target_width,
      .height = target_height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(target_width) * target_height * 4, 0),
  };
  const int left = (target_width - fitted_width) / 2;
  const int top = (target_height - fitted_height) / 2;
  const size_t row_bytes = static_cast<size_t>(fitted_width) * 4;
  for (int y = 0; y < fitted_height; ++y) {
    const size_t source_offset = static_cast<size_t>(y) * row_bytes;
    const size_t target_offset = PixelOffset(framed, left, top + y);
    std::copy_n(fitted.pixels.begin() + static_cast<ptrdiff_t>(source_offset), row_bytes,
                framed.pixels.begin() + static_cast<ptrdiff_t>(target_offset));
  }
  return framed;
}

ParallaxArtworkStageDiagnostic Diagnostic(ParallaxArtworkStage stage, const RgbaImage& image) {
  size_t visible_pixels = 0;
  for (size_t offset = 3; offset < image.pixels.size(); offset += 4) {
    if (image.pixels[offset] != 0) ++visible_pixels;
  }
  return {
      .stage = stage,
      .width = image.width,
      .height = image.height,
      .visible_pixels = visible_pixels,
  };
}

size_t NewlyTransparentPixels(const RgbaImage& before, const RgbaImage& after) {
  size_t removed = 0;
  for (size_t offset = 3; offset < before.pixels.size(); offset += 4) {
    if (before.pixels[offset] != 0 && after.pixels[offset] == 0) ++removed;
  }
  return removed;
}

absl::Status ValidateParallaxPolicies(const ParallaxArtworkPipelineConfig& config) {
  switch (config.frame_policy) {
    case ParallaxArtworkFramePolicy::kCropToFill:
    case ParallaxArtworkFramePolicy::kFitInside:
      break;
    default:
      return absl::InvalidArgumentError("parallax artwork frame policy is invalid");
  }
  switch (config.alpha_role) {
    case ParallaxArtworkAlphaRole::kOpaquePlate:
    case ParallaxArtworkAlphaRole::kTransparentOverlay:
      break;
    default:
      return absl::InvalidArgumentError("parallax artwork alpha role is invalid");
  }
  switch (config.overlay_extraction) {
    case ParallaxArtworkOverlayExtraction::kPreserveAlpha:
    case ParallaxArtworkOverlayExtraction::kRemoveSolidMatte:
      break;
    default:
      return absl::InvalidArgumentError("parallax artwork overlay extraction policy is invalid");
  }
  switch (config.overlay_alpha_policy) {
    case ParallaxArtworkOverlayAlphaPolicy::kPreserve:
    case ParallaxArtworkOverlayAlphaPolicy::kBinary:
      return absl::OkStatus();
  }
  return absl::InvalidArgumentError("parallax artwork overlay alpha policy is invalid");
}

absl::Status ValidateTargetGeometry(const ParallaxArtworkPipelineConfig& config,
                                    const ParallaxArtworkStyle& style) {
  if (config.target_width <= 0 || config.target_height <= 0) {
    return absl::InvalidArgumentError("parallax artwork target dimensions must be positive");
  }
  if (config.target_width > config.source_limits.maximum_width ||
      config.target_height > config.source_limits.maximum_height) {
    return absl::ResourceExhaustedError(
        "parallax artwork target dimensions exceed configured source limits");
  }
  if (config.target_width % style.pixel_block_size != 0 ||
      config.target_height % style.pixel_block_size != 0) {
    return absl::InvalidArgumentError(
        "parallax artwork target dimensions must divide into whole pixel blocks");
  }
  const uint64_t output_pixels = static_cast<uint64_t>(config.target_width) * config.target_height;
  if (output_pixels > config.source_limits.maximum_pixels ||
      output_pixels > config.source_limits.maximum_bytes / 4) {
    return absl::ResourceExhaustedError("parallax artwork output exceeds its configured limits");
  }
  return absl::OkStatus();
}

absl::Status ValidateMatteSettings(const ParallaxArtworkPipelineConfig& config) {
  if (config.matte_color.a != 255) {
    return absl::InvalidArgumentError("parallax artwork matte color must be opaque");
  }
  if (!std::isfinite(config.matte_transparent_distance) ||
      config.matte_transparent_distance < 0.0f) {
    return absl::InvalidArgumentError(
        "parallax artwork transparent matte distance must be finite and non-negative");
  }
  const double maximum_rgb_distance = std::sqrt(3.0 * 255.0 * 255.0);
  if (!std::isfinite(config.matte_opaque_distance) ||
      config.matte_opaque_distance <= config.matte_transparent_distance ||
      config.matte_opaque_distance > maximum_rgb_distance) {
    return absl::InvalidArgumentError(
        "parallax artwork opaque matte distance must exceed the transparent distance and fit RGB");
  }
  if (config.binary_alpha_threshold < 1 || config.binary_alpha_threshold > 255) {
    return absl::InvalidArgumentError(
        "parallax artwork binary alpha threshold must be between 1 and 255");
  }
  return absl::OkStatus();
}

absl::Status ValidateRoleCompatibility(const ParallaxArtworkPipelineConfig& config,
                                       const ParallaxArtworkStyle& style) {
  if (config.alpha_role == ParallaxArtworkAlphaRole::kOpaquePlate) {
    if (config.frame_policy != ParallaxArtworkFramePolicy::kCropToFill) {
      return absl::InvalidArgumentError("opaque parallax plates require crop-to-fill framing");
    }
    if (config.overlay_extraction != ParallaxArtworkOverlayExtraction::kPreserveAlpha ||
        config.overlay_alpha_policy != ParallaxArtworkOverlayAlphaPolicy::kPreserve) {
      return absl::InvalidArgumentError("opaque parallax plates cannot use overlay policies");
    }
    return absl::OkStatus();
  }
  if (config.overlay_extraction == ParallaxArtworkOverlayExtraction::kRemoveSolidMatte &&
      style.palette.empty()) {
    return absl::InvalidArgumentError("solid-matte extraction requires an artwork palette");
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ValidateParallaxArtworkStyle(const ParallaxArtworkStyle& style) {
  if (style.pixel_block_size <= 0 || style.pixel_block_size > 4096) {
    return absl::InvalidArgumentError(
        "parallax artwork pixel block size must be between 1 and 4096");
  }
  if (style.palette.size() > 256) {
    return absl::InvalidArgumentError("parallax artwork palette cannot exceed 256 colors");
  }
  if (style.quantize_to_palette && style.palette.empty()) {
    return absl::InvalidArgumentError("parallax artwork quantization requires a palette");
  }
  std::set<uint32_t> colors;
  for (const RgbaColor& color : style.palette) {
    if (color.a != 255) {
      return absl::InvalidArgumentError("parallax artwork palette colors must be opaque");
    }
    const uint32_t packed = (static_cast<uint32_t>(color.r) << 24) |
                            (static_cast<uint32_t>(color.g) << 16) |
                            (static_cast<uint32_t>(color.b) << 8) | color.a;
    if (!colors.insert(packed).second) {
      return absl::InvalidArgumentError("parallax artwork palette colors must be unique");
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateParallaxArtworkPipelineConfig(const ParallaxArtworkPipelineConfig& config,
                                                   const ParallaxArtworkStyle& style) {
  RETURN_IF_ERROR(ValidateSourceArtworkLimits(config.source_limits));
  RETURN_IF_ERROR(ValidateParallaxArtworkStyle(style));
  RETURN_IF_ERROR(ValidateParallaxPolicies(config));
  RETURN_IF_ERROR(ValidateTargetGeometry(config, style));
  RETURN_IF_ERROR(ValidateMatteSettings(config));
  return ValidateRoleCompatibility(config, style);
}

absl::StatusOr<ParallaxArtworkPipelineResult> RunParallaxArtworkPipeline(
    const RgbaImage& source, const ParallaxArtworkStyle& style,
    const ParallaxArtworkPipelineConfig& config) {
  RETURN_IF_ERROR(ValidateSourceArtworkPixels(source, config.source_limits));
  RETURN_IF_ERROR(ValidateParallaxArtworkPipelineConfig(config, style));
  ASSIGN_OR_RETURN(const std::string source_digest, RgbaImageDigest(source));

  absl::StatusOr<RgbaImage> framed_result =
      config.frame_policy == ParallaxArtworkFramePolicy::kCropToFill
          ? CropToFill(source, config.target_width, config.target_height)
          : FitInside(source, config.target_width, config.target_height);
  if (!framed_result.ok()) {
    return absl::Status(framed_result.status().code(),
                        absl::StrCat("framing: ", framed_result.status().message()));
  }
  RgbaImage framed = std::move(*framed_result);

  GeneratedArtworkPostprocessConfig postprocess{
      .output_width = config.target_width,
      .output_height = config.target_height,
      .pixel_block_size = style.pixel_block_size,
      .background_policy =
          config.overlay_extraction == ParallaxArtworkOverlayExtraction::kRemoveSolidMatte
              ? GeneratedArtworkBackgroundPolicy::kRemoveSolidMatte
              : GeneratedArtworkBackgroundPolicy::kPreserve,
      .palette_policy = style.quantize_to_palette ? GeneratedArtworkPalettePolicy::kQuantize
                                                  : GeneratedArtworkPalettePolicy::kPreserve,
      .alpha_policy = config.alpha_role == ParallaxArtworkAlphaRole::kOpaquePlate
                          ? GeneratedArtworkAlphaPolicy::kOpaque
                      : config.overlay_alpha_policy == ParallaxArtworkOverlayAlphaPolicy::kBinary
                          ? GeneratedArtworkAlphaPolicy::kBinary
                          : GeneratedArtworkAlphaPolicy::kPreserve,
      .background = config.matte_color,
      .transparent_distance = config.matte_transparent_distance,
      .opaque_distance = config.matte_opaque_distance,
      .final_alpha_threshold = config.binary_alpha_threshold,
      .minimum_visible_pixels = 1,
      .minimum_transparent_border = 0,
  };
  ASSIGN_OR_RETURN(GeneratedArtworkPostprocessResult processed,
                   PostprocessGeneratedArtwork(framed, style.palette, postprocess));
  if (config.overlay_extraction == ParallaxArtworkOverlayExtraction::kRemoveSolidMatte &&
      NewlyTransparentPixels(framed, processed.matted) == 0) {
    return absl::FailedPreconditionError(
        "matte extraction found no pixels matching the configured matte");
  }
  RETURN_IF_ERROR(ValidateSourceArtworkPixels(processed.finished, config.source_limits));
  ASSIGN_OR_RETURN(const std::string final_digest, RgbaImageDigest(processed.finished));
  ASSIGN_OR_RETURN(const RepetitionDiagnostics repetition, AnalyzeRepetition(processed.finished));

  const size_t maximum_preview_pixels =
      config.source_limits.maximum_pixels > std::numeric_limits<size_t>::max() / 3
          ? std::numeric_limits<size_t>::max()
          : config.source_limits.maximum_pixels * 3;
  std::optional<RgbaImage> repeat_x_preview;
  if (config.review_repeat_x) {
    ASSIGN_OR_RETURN(repeat_x_preview,
                     BuildRepetitionPreview(processed.finished, 3, 1, maximum_preview_pixels));
  }
  std::optional<RgbaImage> repeat_y_preview;
  if (config.review_repeat_y) {
    ASSIGN_OR_RETURN(repeat_y_preview,
                     BuildRepetitionPreview(processed.finished, 1, 3, maximum_preview_pixels));
  }

  const std::array<ParallaxArtworkStageDiagnostic, 4> stages = {
      Diagnostic(ParallaxArtworkStage::kFraming, framed),
      Diagnostic(ParallaxArtworkStage::kMatteExtraction, processed.matted),
      Diagnostic(ParallaxArtworkStage::kRasterization, processed.resized),
      Diagnostic(ParallaxArtworkStage::kFinished, processed.finished),
  };
  return ParallaxArtworkPipelineResult{
      .source_digest = source_digest,
      .final_digest = final_digest,
      .framed = std::move(framed),
      .matte_extracted = std::move(processed.matted),
      .rasterized = std::move(processed.resized),
      .finished = std::move(processed.finished),
      .stages = stages,
      .repetition = repetition,
      .repeat_x_preview = std::move(repeat_x_preview),
      .repeat_y_preview = std::move(repeat_y_preview),
  };
}

}  // namespace zebes
