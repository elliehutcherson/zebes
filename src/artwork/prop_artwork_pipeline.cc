#include "artwork/prop_artwork_pipeline.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "artwork/rasterize_prop.h"
#include "common/image_digest.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

const char* StageName(PropArtworkStage stage) {
  switch (stage) {
    case PropArtworkStage::kIsolation:
      return "isolation";
    case PropArtworkStage::kComposition:
      return "composition";
    case PropArtworkStage::kRasterization:
      return "rasterization";
    case PropArtworkStage::kQuantization:
      return "quantization";
    case PropArtworkStage::kEdgeTreatment:
      return "edge treatment";
    case PropArtworkStage::kCleanup:
      return "cleanup";
  }
  return "unknown stage";
}

template <typename T>
absl::StatusOr<T> AtStage(PropArtworkStage stage, absl::StatusOr<T> result) {
  if (result.ok()) return result;
  return absl::Status(result.status().code(),
                      absl::StrCat(StageName(stage), ": ", result.status().message()));
}

PropStageDiagnostic Diagnostic(PropArtworkStage stage, const RgbaImage& image) {
  size_t visible_pixels = 0;
  const size_t pixel_count = static_cast<size_t>(image.width) * image.height;
  for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
    if (image.pixels[pixel * 4 + 3] != 0) ++visible_pixels;
  }
  return PropStageDiagnostic{
      .stage = stage,
      .width = image.width,
      .height = image.height,
      .visible_pixels = visible_pixels,
  };
}

}  // namespace

absl::Status ValidatePropSource(const RgbaImage& source, const PropSourceLimits& limits) {
  if (!source.IsValid()) return absl::InvalidArgumentError("source artwork is not valid RGBA8");
  if (limits.maximum_width <= 0 || limits.maximum_height <= 0 || limits.maximum_pixels == 0 ||
      limits.maximum_bytes == 0) {
    return absl::InvalidArgumentError("source artwork limits must be positive");
  }

  const uint64_t pixels = static_cast<uint64_t>(source.width) * source.height;
  if (source.width > limits.maximum_width || source.height > limits.maximum_height ||
      pixels > limits.maximum_pixels || source.pixels.size() > limits.maximum_bytes) {
    return absl::ResourceExhaustedError(absl::StrCat("source artwork ", source.width, "x",
                                                     source.height, " (", source.pixels.size(),
                                                     " bytes) exceeds configured limits"));
  }
  return absl::OkStatus();
}

absl::StatusOr<PropArtworkPipelineResult> RunPropArtworkPipeline(
    const RgbaImage& source, const PropArtworkStyle& style,
    const PropArtworkPipelineConfig& config) {
  RETURN_IF_ERROR(ValidatePropSource(source, config.source_limits));
  RETURN_IF_ERROR(ValidatePropArtworkStyle(style));

  const PropRasterConfig raster_config{
      .tile_size = style.tile_size,
      .canvas_tiles_wide = config.composition.canvas_tiles_wide,
      .canvas_tiles_high = config.composition.canvas_tiles_high,
      .pixel_block_size = style.pixel_block_size,
  };
  const int64_t output_width =
      static_cast<int64_t>(style.tile_size) * config.composition.canvas_tiles_wide;
  const int64_t output_height =
      static_cast<int64_t>(style.tile_size) * config.composition.canvas_tiles_high;
  if (output_width <= 0 || output_height <= 0 || output_width > std::numeric_limits<int>::max() ||
      output_height > std::numeric_limits<int>::max()) {
    return absl::InvalidArgumentError("prop output dimensions overflow integer storage");
  }
  RETURN_IF_ERROR(ValidatePropAttachment(config.composition.attachment,
                                         static_cast<int>(output_width),
                                         static_cast<int>(output_height)));

  ASSIGN_OR_RETURN(const std::string source_digest, RgbaImageDigest(source));
  ASSIGN_OR_RETURN(RgbaImage isolated,
                   AtStage(PropArtworkStage::kIsolation, IsolateSubject(source, config.isolation)));
  ASSIGN_OR_RETURN(PropArtwork composed,
                   AtStage(PropArtworkStage::kComposition,
                           ComposeProp(isolated, config.composition, static_cast<int>(output_width),
                                       static_cast<int>(output_height))));
  ASSIGN_OR_RETURN(PropArtwork rasterized, AtStage(PropArtworkStage::kRasterization,
                                                   RasterizeProp(composed, raster_config)));
  if (config.composition.attachment.mode == PropAttachmentMode::kFree) {
    rasterized.anchor_x = config.composition.attachment.free_anchor->x;
    rasterized.anchor_y = config.composition.attachment.free_anchor->y;
  }
  ASSIGN_OR_RETURN(PropPalette palette,
                   AtStage(PropArtworkStage::kQuantization, BuildPropPalette(style.palette)));
  ASSIGN_OR_RETURN(PropArtwork quantized,
                   AtStage(PropArtworkStage::kQuantization, QuantizeProp(rasterized, palette)));
  ASSIGN_OR_RETURN(PropArtwork edge_treated,
                   AtStage(PropArtworkStage::kEdgeTreatment,
                           ApplyPropEdgeTreatment(quantized, palette.outline, config.edge)));
  ASSIGN_OR_RETURN(
      PropArtwork finished,
      AtStage(PropArtworkStage::kCleanup,
              CleanupAndValidateProp(edge_treated, palette.colors, style.tile_size, config.cleanup,
                                     config.composition.attachment.mode)));

  const std::array<PropStageDiagnostic, 6> diagnostics = {
      Diagnostic(PropArtworkStage::kIsolation, isolated),
      Diagnostic(PropArtworkStage::kComposition, composed.image),
      Diagnostic(PropArtworkStage::kRasterization, rasterized.image),
      Diagnostic(PropArtworkStage::kQuantization, quantized.image),
      Diagnostic(PropArtworkStage::kEdgeTreatment, edge_treated.image),
      Diagnostic(PropArtworkStage::kCleanup, finished.image),
  };
  return PropArtworkPipelineResult{
      .source_digest = source_digest,
      .palette = std::move(palette),
      .isolated = std::move(isolated),
      .composed = std::move(composed),
      .rasterized = std::move(rasterized),
      .quantized = std::move(quantized),
      .edge_treated = std::move(edge_treated),
      .finished = std::move(finished),
      .diagnostics = diagnostics,
  };
}

}  // namespace zebes
