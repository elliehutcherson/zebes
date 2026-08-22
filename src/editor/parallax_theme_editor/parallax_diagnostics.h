#pragma once

#include <optional>

#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "editor/parallax_theme_editor/parallax_preview_model.h"
#include "objects/camera.h"
#include "objects/game_view.h"
#include "objects/parallax_theme.h"

namespace zebes {

struct OpposingEdgeDifference {
  int pixels_compared = 0;
  int exact_pixel_matches = 0;
  double mean_absolute_channel_difference = 0.0;
  int maximum_channel_difference = 0;
};

struct RepetitionDiagnostics {
  OpposingEdgeDifference horizontal;
  OpposingEdgeDifference vertical;
};

struct CoverageAxisDiagnostics {
  bool repeated = false;
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

// Compares left/right and top/bottom edge pixels. These are measured facts for
// human seam review, not a claim that a texture is or is not seamless.
absl::StatusOr<RepetitionDiagnostics> AnalyzeRepetition(const RgbaImage& image);

// Measures the smallest non-repeating texture margin while a camera moves
// between the route endpoints at both zoom extremes. Negative margins are
// uncovered world units. Repeating axes are always considered covered.
absl::StatusOr<CameraCoverageDiagnostics> AnalyzeCameraCoverage(
    const ParallaxLayer& layer, int texture_width, int texture_height, Vec route_min, Vec route_max,
    const GameViewSize& game_view, CameraZoomRange zoom_range,
    std::optional<CameraWorldBounds> world = std::nullopt);

}  // namespace zebes
