#include "editor/parallax_theme_editor/parallax_diagnostics.h"

#include <array>
#include <limits>

#include "absl/status/status.h"
#include "common/status_macros.h"
#include "engine/parallax_layout.h"

namespace zebes {
namespace {

void AnalyzeAxisAtZoom(CoverageAxisDiagnostics& result, double content_min, double content_max,
                       double route_start, double route_end, int viewport_size, double zoom,
                       double scroll_factor, double offset) {
  const std::array<double, 2> positions = {route_start, route_end};
  for (double position : positions) {
    const double visible_start = position - viewport_size / (2.0 * zoom);
    const double visible_end = position + viewport_size / (2.0 * zoom);
    const double layer_origin = offset + (visible_start - offset) * (1.0 - scroll_factor);
    result.minimum_start_margin =
        std::min(result.minimum_start_margin, visible_start - (layer_origin + content_min));
    result.minimum_end_margin =
        std::min(result.minimum_end_margin, layer_origin + content_max - visible_end);
  }
}

CoverageAxisDiagnostics InitialAxisDiagnostics(double content_span, double repeat_period) {
  const bool repeated = repeat_period > 0.0;
  CoverageAxisDiagnostics result{
      .repeated = repeated,
      .composition_span = content_span,
      .repeat_period = repeat_period,
      .period_minus_span = repeated ? repeat_period - content_span : 0.0,
      .minimum_start_margin = std::numeric_limits<double>::infinity(),
      .minimum_end_margin = std::numeric_limits<double>::infinity(),
  };
  if (repeated) {
    result.minimum_start_margin = 0.0;
    result.minimum_end_margin = 0.0;
  }
  return result;
}

}  // namespace

absl::StatusOr<CompositionSeamDiagnostics> AnalyzeCompositionSeams(
    const ParallaxLayer& layer, const std::vector<ParallaxElementSize>& element_sizes) {
  ASSIGN_OR_RETURN(const std::vector<ParallaxElementBounds> bounds,
                   CalculateParallaxElementBounds(layer, element_sizes));
  CompositionSeamDiagnostics result;
  auto make_seam = [](const ParallaxElementBounds& first, const ParallaxElementBounds& second,
                      Vec second_offset = Vec{}) {
    return ElementSeamDiagnostics{
        .first_element_id = first.element_id,
        .second_element_id = second.element_id,
        .separation =
            {
                second.bounds.min.x + second_offset.x - first.bounds.max.x,
                second.bounds.min.y + second_offset.y - first.bounds.max.y,
            },
    };
  };
  for (size_t index = 1; index < bounds.size(); ++index) {
    result.adjacent.push_back(make_seam(bounds[index - 1], bounds[index]));
  }
  if (layer.repeat_period.x > 0.0) {
    result.horizontal_wrap = make_seam(bounds.back(), bounds.front(), {layer.repeat_period.x, 0.0});
  }
  if (layer.repeat_period.y > 0.0) {
    result.vertical_wrap = make_seam(bounds.back(), bounds.front(), {0.0, layer.repeat_period.y});
  }
  return result;
}

absl::StatusOr<CameraCoverageDiagnostics> AnalyzeCameraCoverage(
    const ParallaxLayer& layer, WorldRect composition_bounds, Vec route_min, Vec route_max,
    const GameViewSize& game_view, CameraZoomRange zoom_range,
    std::optional<CameraWorldBounds> world) {
  if (!composition_bounds.IsValid() || !game_view.IsValid() || !zoom_range.IsValid() ||
      !std::isfinite(route_min.x) || !std::isfinite(route_min.y) || !std::isfinite(route_max.x) ||
      !std::isfinite(route_max.y) || route_min.x > route_max.x || route_min.y > route_max.y ||
      !std::isfinite(layer.scroll_factor.x) || !std::isfinite(layer.scroll_factor.y) ||
      !std::isfinite(layer.offset.x) || !std::isfinite(layer.offset.y) ||
      !std::isfinite(layer.repeat_period.x) || !std::isfinite(layer.repeat_period.y) ||
      layer.repeat_period.x < 0.0 || layer.repeat_period.y < 0.0) {
    return absl::InvalidArgumentError("camera coverage diagnostics require valid geometry");
  }

  CameraCoverageDiagnostics result{
      .horizontal = InitialAxisDiagnostics(composition_bounds.max.x - composition_bounds.min.x,
                                           layer.repeat_period.x),
      .vertical = InitialAxisDiagnostics(composition_bounds.max.y - composition_bounds.min.y,
                                         layer.repeat_period.y),
  };
  for (const double zoom : {zoom_range.minimum, zoom_range.maximum}) {
    ASSIGN_OR_RETURN(
        const CameraCenterRoute route,
        ResolveCameraCenterRoute({.min = route_min, .max = route_max}, game_view, zoom, world));
    if (layer.repeat_period.x == 0.0) {
      AnalyzeAxisAtZoom(result.horizontal, composition_bounds.min.x, composition_bounds.max.x,
                        route.min.x, route.max.x, game_view.width, zoom, layer.scroll_factor.x,
                        layer.offset.x);
    }
    if (layer.repeat_period.y == 0.0) {
      AnalyzeAxisAtZoom(result.vertical, composition_bounds.min.y, composition_bounds.max.y,
                        route.min.y, route.max.y, game_view.height, zoom, layer.scroll_factor.y,
                        layer.offset.y);
    }
  }
  return result;
}

}  // namespace zebes
