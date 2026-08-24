#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/cleanup_prop.h"
#include "artwork/compose_prop.h"
#include "artwork/edge_treatment.h"
#include "artwork/isolate_subject.h"
#include "artwork/prop_artwork.h"
#include "artwork/prop_artwork_style.h"
#include "artwork/quantize_prop.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"

namespace zebes {

inline constexpr int kPropArtworkPipelineVersion = 2;

enum class PropArtworkStage : uint8_t {
  kIsolation = 0,
  kComposition = 1,
  kRasterization = 2,
  kQuantization = 3,
  kEdgeTreatment = 4,
  kCleanup = 5,
};

struct PropArtworkPipelineConfig {
  SourceArtworkLimits source_limits;
  SubjectIsolationConfig isolation;
  PropCompositionConfig composition;
  PropEdgeConfig edge;
  PropCleanupConfig cleanup;
};

// Stable, platform-neutral facts suitable for a stage preview or status panel.
// More specialized diagnostics can be added to a stage without changing how
// the editor owns its pixels.
struct PropStageDiagnostic {
  PropArtworkStage stage = PropArtworkStage::kIsolation;
  int width = 0;
  int height = 0;
  size_t visible_pixels = 0;
};

// Retains each deterministic artifact so review-each-step and finished-only
// modes can run the same coordinator. The editor may discard intermediate
// images after a finished-only run.
struct PropArtworkPipelineResult {
  int pipeline_version = kPropArtworkPipelineVersion;
  std::string source_digest;
  PropPalette palette;
  RgbaImage isolated;
  PropArtwork composed;
  PropArtwork rasterized;
  PropArtwork quantized;
  PropArtwork edge_treated;
  PropArtwork finished;
  std::array<PropStageDiagnostic, 6> diagnostics;
};

absl::Status ValidatePropArtworkPipelineConfig(const PropArtworkPipelineConfig& config,
                                               const PropArtworkStyle& style);

absl::StatusOr<PropArtworkPipelineResult> RunPropArtworkPipeline(
    const RgbaImage& source, const PropArtworkStyle& style,
    const PropArtworkPipelineConfig& config);

}  // namespace zebes
