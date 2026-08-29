#pragma once

#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "artwork/repetition_review.h"
#include "editor/parallax_theme_editor/parallax_preview_model.h"
#include "engine/parallax_layout.h"
#include "objects/camera.h"
#include "objects/game_view.h"
#include "objects/parallax_theme.h"

namespace zebes {

struct CoverageAxisDiagnostics {
  bool repeated = false;
  double composition_span = 0.0;
  double repeat_period = 0.0;
  // Positive means adjacent composition bounds leave space; negative means
  // they overlap. Artwork transparency still requires human seam review.
  double period_minus_span = 0.0;
  double minimum_start_margin = 0.0;
  double minimum_end_margin = 0.0;

  bool covers() const {
    return repeated || (minimum_start_margin >= 0.0 && minimum_end_margin >= 0.0);
  }
};

struct CameraCoverageDiagnostics {
  CoverageAxisDiagnostics horizontal;
  CoverageAxisDiagnostics vertical;
};

struct ElementSeamDiagnostics {
  int first_element_id = -1;
  int second_element_id = -1;
  // Positive is a bounds gap; negative is overlap. Each component compares
  // the second minimum with the first maximum on that axis.
  Vec separation;
};

struct CompositionSeamDiagnostics {
  std::vector<ElementSeamDiagnostics> adjacent;
  std::optional<ElementSeamDiagnostics> horizontal_wrap;
  std::optional<ElementSeamDiagnostics> vertical_wrap;
};

absl::StatusOr<CompositionSeamDiagnostics> AnalyzeCompositionSeams(
    const ParallaxLayer& layer, const std::vector<ParallaxElementSize>& element_sizes);

// Measures the smallest non-repeating composition margin while a camera moves
// between the route endpoints at both zoom extremes. Negative margins are
// uncovered world units. Repeating axes are always considered covered.
absl::StatusOr<CameraCoverageDiagnostics> AnalyzeCameraCoverage(
    const ParallaxLayer& layer, WorldRect composition_bounds, Vec route_min, Vec route_max,
    const GameViewSize& game_view, CameraZoomRange zoom_range,
    std::optional<CameraWorldBounds> world = std::nullopt);

}  // namespace zebes
