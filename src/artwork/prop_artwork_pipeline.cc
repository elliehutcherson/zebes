#include "artwork/prop_artwork_pipeline.h"

#include <array>
#include <cstddef>
#include <cstdint>
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

absl::Status ValidatePropArtworkPipelineConfig(const PropArtworkPipelineConfig& config,
                                               const PropArtworkStyle& style) {
  RETURN_IF_ERROR(ValidatePropArtworkStyle(style));
  RETURN_IF_ERROR(ValidateSourceArtworkLimits(config.source_limits));
  RETURN_IF_ERROR(ValidateSubjectIsolationConfig(config.isolation));
  RETURN_IF_ERROR(ValidatePropCompositionConfig(config.composition));
  RETURN_IF_ERROR(ValidatePropEdgeConfig(config.edge));
  RETURN_IF_ERROR(ValidatePropCleanupConfig(config.cleanup));

  const PropRasterConfig raster{
      .tile_size = style.tile_size,
      .canvas_tiles_wide = config.composition.canvas_tiles_wide,
      .canvas_tiles_high = config.composition.canvas_tiles_high,
      .pixel_block_size = style.pixel_block_size,
  };
  RETURN_IF_ERROR(ValidatePropRasterConfig(raster));
  const int output_width = style.tile_size * config.composition.canvas_tiles_wide;
  const int output_height = style.tile_size * config.composition.canvas_tiles_high;
  return ValidatePropAttachment(config.composition.attachment, output_width, output_height);
}

absl::StatusOr<PropArtworkPipelineResult> RunPropArtworkPipeline(
    const RgbaImage& source, const PropArtworkStyle& style,
    const PropArtworkPipelineConfig& config) {
  RETURN_IF_ERROR(ValidateSourceArtworkPixels(source, config.source_limits));
  RETURN_IF_ERROR(ValidatePropArtworkPipelineConfig(config, style));

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
