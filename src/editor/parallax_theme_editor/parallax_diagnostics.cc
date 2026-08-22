#include "editor/parallax_theme_editor/parallax_diagnostics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "absl/status/status.h"

namespace zebes {
namespace {

OpposingEdgeDifference CompareEdges(const RgbaImage& image, bool horizontal) {
  const int pixel_count = horizontal ? image.height : image.width;
  OpposingEdgeDifference result{.pixels_compared = pixel_count};
  int64_t total_difference = 0;

  for (int pixel = 0; pixel < pixel_count; ++pixel) {
    const int first_x = horizontal ? 0 : pixel;
    const int first_y = horizontal ? pixel : 0;
    const int second_x = horizontal ? image.width - 1 : pixel;
    const int second_y = horizontal ? pixel : image.height - 1;
    const size_t first_offset = static_cast<size_t>(first_y * image.width + first_x) * 4;
    const size_t second_offset = static_cast<size_t>(second_y * image.width + second_x) * 4;
    bool exact_match = true;
    for (int channel = 0; channel < 4; ++channel) {
      const int difference = std::abs(static_cast<int>(image.pixels[first_offset + channel]) -
                                      static_cast<int>(image.pixels[second_offset + channel]));
      total_difference += difference;
      result.maximum_channel_difference = std::max(result.maximum_channel_difference, difference);
      exact_match = exact_match && difference == 0;
    }
    if (exact_match) ++result.exact_pixel_matches;
  }

  result.mean_absolute_channel_difference =
      static_cast<double>(total_difference) / (pixel_count * 4);
  return result;
}

CoverageAxisDiagnostics AnalyzeAxis(bool repeated, double texture_size, double route_start,
                                    double route_end, int viewport_size, CameraZoomRange zoom_range,
                                    double scroll_factor, double offset) {
  CoverageAxisDiagnostics result{
      .repeated = repeated,
      .minimum_start_margin = std::numeric_limits<double>::infinity(),
      .minimum_end_margin = std::numeric_limits<double>::infinity(),
  };
  if (repeated) {
    result.minimum_start_margin = 0.0;
    result.minimum_end_margin = 0.0;
    return result;
  }

  const std::array<double, 2> positions = {route_start, route_end};
  const std::array<double, 2> zooms = {zoom_range.minimum, zoom_range.maximum};
  for (double position : positions) {
    for (double zoom : zooms) {
      const double visible_start = position - viewport_size / (2.0 * zoom);
      const double visible_end = position + viewport_size / (2.0 * zoom);
      const double layer_origin = offset + (visible_start - offset) * (1.0 - scroll_factor);
      result.minimum_start_margin =
          std::min(result.minimum_start_margin, visible_start - layer_origin);
      result.minimum_end_margin =
          std::min(result.minimum_end_margin, layer_origin + texture_size - visible_end);
    }
  }
  return result;
}

}  // namespace

absl::StatusOr<RepetitionDiagnostics> AnalyzeRepetition(const RgbaImage& image) {
  if (!image.IsValid()) {
    return absl::InvalidArgumentError("repetition diagnostics require a valid RGBA image");
  }
  return RepetitionDiagnostics{
      .horizontal = CompareEdges(image, true),
      .vertical = CompareEdges(image, false),
  };
}

absl::StatusOr<CameraCoverageDiagnostics> AnalyzeCameraCoverage(
    const ParallaxLayer& layer, int texture_width, int texture_height, Vec route_min, Vec route_max,
    const GameViewSize& game_view, CameraZoomRange zoom_range) {
  if (texture_width <= 0 || texture_height <= 0 || !game_view.IsValid() || !zoom_range.IsValid() ||
      !std::isfinite(route_min.x) || !std::isfinite(route_min.y) || !std::isfinite(route_max.x) ||
      !std::isfinite(route_max.y) || route_min.x > route_max.x || route_min.y > route_max.y ||
      !std::isfinite(layer.base_scale) || layer.base_scale <= 0.0f ||
      !std::isfinite(layer.scroll_factor.x) || !std::isfinite(layer.scroll_factor.y) ||
      !std::isfinite(layer.offset.x) || !std::isfinite(layer.offset.y)) {
    return absl::InvalidArgumentError("camera coverage diagnostics require valid geometry");
  }

  return CameraCoverageDiagnostics{
      .horizontal =
          AnalyzeAxis(layer.repeat_x, texture_width * layer.base_scale, route_min.x, route_max.x,
                      game_view.width, zoom_range, layer.scroll_factor.x, layer.offset.x),
      .vertical =
          AnalyzeAxis(layer.repeat_y, texture_height * layer.base_scale, route_min.y, route_max.y,
                      game_view.height, zoom_range, layer.scroll_factor.y, layer.offset.y),
  };
}

}  // namespace zebes
