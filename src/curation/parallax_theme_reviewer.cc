#include "curation/parallax_theme_reviewer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "curation/raster_canvas.h"
#include "editor/level_editor/parallax_layout.h"
#include "editor/parallax_theme_editor/parallax_diagnostics.h"
#include "editor/parallax_theme_editor/parallax_preview_model.h"

namespace zebes {
namespace {

struct ParallaxReviewContext {
  std::string path_id;
  std::string subject;
  CameraCenterRoute route;
  std::optional<CameraWorldBounds> world;
  nlohmann::json metadata;
};

struct RouteSample {
  const char* id;
  double progress;
};

constexpr std::array<double, 3> kReviewZooms = {0.5, 1.0, 2.0};
constexpr std::array<RouteSample, 3> kRouteSamples = {
    RouteSample{.id = "start", .progress = 0.0},
    RouteSample{.id = "middle", .progress = 0.5},
    RouteSample{.id = "end", .progress = 1.0},
};

std::string ZoomId(double zoom) {
  return absl::StrCat("z", static_cast<int>(std::lround(zoom * 100.0)));
}

const ParallaxElement* FindElement(const ParallaxLayer& layer, int element_id) {
  for (const ParallaxElement& element : layer.elements) {
    if (element.id == element_id) return &element;
  }
  return nullptr;
}

absl::StatusOr<std::map<std::string, RgbaImage>> LoadThemeTextures(Api& api,
                                                                   const ParallaxTheme& theme) {
  std::map<std::string, RgbaImage> images;
  for (const ParallaxLayer& layer : theme.layers) {
    for (const ParallaxElement& element : layer.elements) {
      if (images.contains(element.texture_id)) continue;
      ASSIGN_OR_RETURN(RgbaImage image, api.ReadTexturePixels(element.texture_id));
      if (!image.IsValid()) {
        return absl::DataLossError(
            absl::StrCat("parallax texture is invalid: ", element.texture_id));
      }
      images.emplace(element.texture_id, std::move(image));
    }
  }
  return images;
}

absl::StatusOr<std::vector<ParallaxElementSize>> ResolveElementSizes(
    const ParallaxLayer& layer, const std::map<std::string, RgbaImage>& images) {
  std::vector<ParallaxElementSize> sizes;
  sizes.reserve(layer.elements.size());
  for (const ParallaxElement& element : layer.elements) {
    const auto image = images.find(element.texture_id);
    if (image == images.end()) {
      return absl::FailedPreconditionError(
          absl::StrCat("parallax element has no loaded texture: ", element.name));
    }
    sizes.push_back({
        .element_id = element.id,
        .width = image->second.width,
        .height = image->second.height,
    });
  }
  return sizes;
}

absl::StatusOr<RgbaImage> RenderTheme(const ParallaxTheme& theme, const Camera& camera,
                                      const std::map<std::string, RgbaImage>& images,
                                      std::optional<size_t> only_layer = std::nullopt) {
  ASSIGN_OR_RETURN(RgbaImage canvas,
                   CreateSolidRgbaImage(camera.viewport_width, camera.viewport_height,
                                        {.red = 7, .green = 0, .blue = 13, .alpha = 255}));
  for (size_t layer_index = 0; layer_index < theme.layers.size(); ++layer_index) {
    if (only_layer.has_value() && *only_layer != layer_index) continue;
    const ParallaxLayer& layer = theme.layers[layer_index];
    ASSIGN_OR_RETURN(const std::vector<ParallaxElementSize> sizes,
                     ResolveElementSizes(layer, images));
    ASSIGN_OR_RETURN(const ParallaxLayout layout, CalculateParallaxLayout(camera, layer, sizes));
    for (const ParallaxElementLayout& item : layout.elements) {
      const ParallaxElement* element = FindElement(layer, item.element_id);
      if (element == nullptr) {
        return absl::InternalError("parallax layout referenced an unknown element");
      }
      const RgbaImage& source = images.at(element->texture_id);
      const Vec minimum = camera.WorldToScreen(item.bounds.min);
      const Vec maximum = camera.WorldToScreen(item.bounds.max);
      RETURN_IF_ERROR(CompositeRgbaNearest(
          canvas, source, {.x = 0, .y = 0, .width = source.width, .height = source.height},
          {.x = minimum.x,
           .y = minimum.y,
           .width = maximum.x - minimum.x,
           .height = maximum.y - minimum.y}));
    }
  }
  return canvas;
}

absl::StatusOr<std::vector<ParallaxReviewContext>> BuildContexts(Api& api,
                                                                 const ParallaxTheme& theme) {
  const std::vector<ParallaxThemeUsage> usages =
      FindParallaxThemeUsages(api.GetAllLevels(), theme.id);
  std::vector<ParallaxReviewContext> contexts;
  contexts.reserve(std::max<size_t>(1, usages.size()));
  for (size_t index = 0; index < usages.size(); ++index) {
    const ParallaxThemeUsage& usage = usages[index];
    contexts.push_back({
        .path_id = absl::StrCat("context-", index),
        .subject = absl::StrCat(usage.level_name, " / ", usage.zone_name),
        .route = usage.route,
        .world = usage.world,
        .metadata =
            {
                {"mode", "level-zone"},
                {"level_id", usage.level_id},
                {"level_name", usage.level_name},
                {"zone_id", usage.zone_id},
                {"zone_name", usage.zone_name},
            },
    });
  }
  if (!contexts.empty()) return contexts;

  ASSIGN_OR_RETURN(CameraCenterRoute route,
                   CalculateContentCameraRoute(theme, api.GetConfig()->game_view));
  route = EnsureNavigableManualCameraRoute(route, api.GetConfig()->game_view);
  contexts.push_back({
      .path_id = "manual",
      .subject = "Unassigned theme content route",
      .route = route,
      .metadata = {{"mode", "content-route"}},
  });
  return contexts;
}

void AddSeamFinding(CurationReview& review, const std::string& subject, std::string axis,
                    const ElementSeamDiagnostics& seam) {
  const double separation = axis == "horizontal" ? seam.separation.x : seam.separation.y;
  std::string code = "geometric-wrap-contact";
  if (separation > 0.0) code = "geometric-wrap-gap";
  if (separation < 0.0) code = "geometric-wrap-overlap";
  review.findings.push_back({
      .severity =
          separation > 0.0 ? CurationFindingSeverity::kWarning : CurationFindingSeverity::kInfo,
      .code = std::move(code),
      .subject = subject,
      .message = absl::StrCat(axis, " wrap separation is ", separation,
                              " world pixels; transparency still requires visual review"),
  });
}

absl::Status AddCoverageFindings(CurationReview& review, const ParallaxTheme& theme,
                                 const std::map<std::string, RgbaImage>& images,
                                 const ParallaxReviewContext& context,
                                 const GameViewSize& game_view) {
  for (const ParallaxLayer& layer : theme.layers) {
    ASSIGN_OR_RETURN(const std::vector<ParallaxElementSize> sizes,
                     ResolveElementSizes(layer, images));
    ASSIGN_OR_RETURN(const WorldRect bounds, CalculateParallaxCompositionBounds(layer, sizes));
    ASSIGN_OR_RETURN(const CameraCoverageDiagnostics coverage,
                     AnalyzeCameraCoverage(layer, bounds, context.route.min, context.route.max,
                                           game_view, kParallaxAuthoringZoomRange, context.world));
    if (!coverage.horizontal.covers() || !coverage.vertical.covers()) {
      review.findings.push_back({
          .severity = CurationFindingSeverity::kWarning,
          .code = "camera-coverage-gap",
          .subject = absl::StrCat(context.subject, " / ", layer.name),
          .message = "finite layer geometry does not cover the complete authoring route",
      });
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<Camera> BuildSeamCamera(const ParallaxLayer& layer,
                                       const std::vector<ParallaxElementSize>& sizes,
                                       const Camera& fallback, bool horizontal) {
  ASSIGN_OR_RETURN(const std::vector<ParallaxElementBounds> bounds,
                   CalculateParallaxElementBounds(layer, sizes));
  const double period = horizontal ? layer.repeat_period.x : layer.repeat_period.y;
  const double scroll = horizontal ? layer.scroll_factor.x : layer.scroll_factor.y;
  const double offset = horizontal ? layer.offset.x : layer.offset.y;
  if (period <= 0.0 || scroll == 0.0) {
    return absl::FailedPreconditionError(
        "a repeated seam cannot be centered when its axis is pinned to the camera");
  }
  const double last_max = horizontal ? bounds.back().bounds.max.x : bounds.back().bounds.max.y;
  const double last_min = horizontal ? bounds.back().bounds.min.x : bounds.back().bounds.min.y;
  const double next_min =
      (horizontal ? bounds.front().bounds.min.x : bounds.front().bounds.min.y) + period;
  const double next_max =
      (horizontal ? bounds.front().bounds.max.x : bounds.front().bounds.max.y) + period;
  const int viewport = horizontal ? fallback.viewport_width : fallback.viewport_height;
  const double target_min = std::min(last_max, next_min) - (last_max - last_min) * 0.25;
  const double target_max = std::max(last_max, next_min) + (next_max - next_min) * 0.25;
  const double seam_center = (target_min + target_max) * 0.5;
  const double fitted_zoom = std::clamp(viewport * 0.8 / (target_max - target_min), 0.125, 2.0);
  const double half_world = viewport / (2.0 * fitted_zoom);
  const double camera_position = offset + (seam_center - half_world * (1.0 - scroll)) / scroll;
  if (!std::isfinite(camera_position)) {
    return absl::InvalidArgumentError("parallax seam camera is not finite");
  }

  Camera result = fallback;
  result.zoom = fitted_zoom;
  if (horizontal) {
    result.position.x = camera_position;
  } else {
    result.position.y = camera_position;
  }
  return result;
}

absl::StatusOr<CurationReview> BuildReview(Api& api, const ParallaxTheme& theme,
                                           std::string_view kind) {
  RETURN_IF_ERROR(ValidateParallaxTheme(theme));
  const GameViewSize game_view = api.GetConfig()->game_view;
  if (!game_view.IsValid()) return absl::FailedPreconditionError("game view is invalid");

  ASSIGN_OR_RETURN(const auto images, LoadThemeTextures(api, theme));
  ASSIGN_OR_RETURN(const std::vector<ParallaxReviewContext> contexts, BuildContexts(api, theme));

  CurationReview review{
      .kind = std::string(kind),
      .asset_id = theme.id,
      .asset_name = theme.name,
      .metadata =
          {
              {"definition", ParallaxThemeToJson(theme)},
              {"game_view", {{"width", game_view.width}, {"height", game_view.height}}},
              {"context_count", contexts.size()},
              {"layer_count", theme.layers.size()},
              {"zoom_range",
               {{"minimum", kParallaxAuthoringZoomRange.minimum},
                {"maximum", kParallaxAuthoringZoomRange.maximum}}},
          },
  };

  for (const ParallaxReviewContext& context : contexts) {
    RETURN_IF_ERROR(AddCoverageFindings(review, theme, images, context, game_view));
    for (const double zoom : kReviewZooms) {
      ASSIGN_OR_RETURN(const CameraCenterRoute route,
                       ResolveCameraCenterRoute(context.route, game_view, zoom, context.world));
      for (const RouteSample& sample : kRouteSamples) {
        const Camera camera{
            .position = InterpolateCameraCenter(route, sample.progress, sample.progress),
            .zoom = zoom,
            .viewport_width = game_view.width,
            .viewport_height = game_view.height,
        };
        ASSIGN_OR_RETURN(RgbaImage image, RenderTheme(theme, camera, images));
        const std::string id =
            absl::StrCat(context.path_id, "-complete-", ZoomId(zoom), "-", sample.id);
        review.artifacts.push_back({
            .id = id,
            .relative_path = absl::StrCat("contexts/", context.path_id, "/complete-", ZoomId(zoom),
                                          "-", sample.id, ".png"),
            .description = absl::StrCat("Complete theme at ", context.subject, ", zoom ", zoom,
                                        ", route ", sample.id),
            .image = std::move(image),
            .metadata =
                {
                    {"context", context.metadata},
                    {"zoom", zoom},
                    {"route_progress", sample.progress},
                    {"camera", {{"x", camera.position.x}, {"y", camera.position.y}}},
                },
        });
      }
    }

    ASSIGN_OR_RETURN(const CameraCenterRoute middle_route,
                     ResolveCameraCenterRoute(context.route, game_view, 1.0, context.world));
    const Camera middle_camera{
        .position = InterpolateCameraCenter(middle_route, 0.5, 0.5),
        .zoom = 1.0,
        .viewport_width = game_view.width,
        .viewport_height = game_view.height,
    };
    for (size_t layer_index = 0; layer_index < theme.layers.size(); ++layer_index) {
      ASSIGN_OR_RETURN(RgbaImage image, RenderTheme(theme, middle_camera, images, layer_index));
      review.artifacts.push_back({
          .id = absl::StrCat(context.path_id, "-layer-", layer_index),
          .relative_path =
              absl::StrCat("contexts/", context.path_id, "/layers/layer-", layer_index, ".png"),
          .description = absl::StrCat("Isolated layer: ", theme.layers[layer_index].name),
          .image = std::move(image),
          .metadata =
              {
                  {"context", context.metadata},
                  {"layer_index", layer_index},
                  {"layer_name", theme.layers[layer_index].name},
                  {"zoom", 1.0},
              },
      });
    }
  }

  const ParallaxReviewContext& seam_context = contexts.front();
  ASSIGN_OR_RETURN(
      const CameraCenterRoute seam_route,
      ResolveCameraCenterRoute(seam_context.route, game_view, 1.0, seam_context.world));
  const Camera seam_fallback{
      .position = InterpolateCameraCenter(seam_route, 0.5, 0.5),
      .zoom = 1.0,
      .viewport_width = game_view.width,
      .viewport_height = game_view.height,
  };
  for (size_t layer_index = 0; layer_index < theme.layers.size(); ++layer_index) {
    const ParallaxLayer& layer = theme.layers[layer_index];
    ASSIGN_OR_RETURN(const std::vector<ParallaxElementSize> sizes,
                     ResolveElementSizes(layer, images));
    ASSIGN_OR_RETURN(const CompositionSeamDiagnostics diagnostics,
                     AnalyzeCompositionSeams(layer, sizes));
    for (const bool horizontal : {true, false}) {
      const double period = horizontal ? layer.repeat_period.x : layer.repeat_period.y;
      if (period <= 0.0) continue;
      const std::string axis = horizontal ? "horizontal" : "vertical";
      const std::optional<ElementSeamDiagnostics>& seam =
          horizontal ? diagnostics.horizontal_wrap : diagnostics.vertical_wrap;
      if (seam.has_value()) {
        AddSeamFinding(review, layer.name, axis, *seam);
      }
      absl::StatusOr<Camera> camera = BuildSeamCamera(layer, sizes, seam_fallback, horizontal);
      if (!camera.ok()) {
        review.findings.push_back({
            .severity = CurationFindingSeverity::kWarning,
            .code = "seam-preview-unavailable",
            .subject = layer.name,
            .message = std::string(camera.status().message()),
        });
        continue;
      }
      ASSIGN_OR_RETURN(RgbaImage image, RenderTheme(theme, *camera, images, layer_index));
      review.artifacts.push_back({
          .id = absl::StrCat("seam-layer-", layer_index, "-", axis),
          .relative_path = absl::StrCat("seams/layer-", layer_index, "-", axis, ".png"),
          .description = absl::StrCat("Centered ", axis, " repeat seam for ", layer.name),
          .image = std::move(image),
          .metadata =
              {
                  {"layer_index", layer_index},
                  {"layer_name", layer.name},
                  {"axis", axis},
                  {"zoom", camera->zoom},
                  {"camera", {{"x", camera->position.x}, {"y", camera->position.y}}},
              },
      });
    }
  }

  RETURN_IF_ERROR(ValidateCurationReview(review));
  return review;
}

absl::StatusOr<ParallaxTheme> ParseCandidate(Api& api, const CurationReviewRequest& request,
                                             const nlohmann::json& candidate) {
  ASSIGN_OR_RETURN(ParallaxTheme * current, api.GetParallaxTheme(request.asset_id));
  if (current == nullptr) return absl::FailedPreconditionError("theme lookup returned null");
  ASSIGN_OR_RETURN(ParallaxTheme parsed, ParallaxThemeFromJson(candidate));
  if (parsed.id != request.asset_id || parsed.id != current->id) {
    return absl::InvalidArgumentError(
        "parallax candidate ID must match the selected existing theme");
  }
  return parsed;
}

}  // namespace

absl::StatusOr<CurationReview> ParallaxThemeReviewer::Review(
    Api& api, const CurationReviewRequest& request) const {
  ASSIGN_OR_RETURN(ParallaxTheme * theme, api.GetParallaxTheme(request.asset_id));
  if (theme == nullptr) return absl::FailedPreconditionError("theme lookup returned null");
  return BuildReview(api, *theme, kind());
}

absl::StatusOr<CurationReview> ParallaxThemeReviewer::ReviewCandidate(
    Api& api, const CurationReviewRequest& request, const nlohmann::json& candidate) const {
  ASSIGN_OR_RETURN(const ParallaxTheme theme, ParseCandidate(api, request, candidate));
  ASSIGN_OR_RETURN(CurationReview review, BuildReview(api, theme, kind()));
  review.metadata["candidate"] = true;
  return review;
}

absl::Status ParallaxThemeReviewer::CommitCandidate(Api& api, const CurationReviewRequest& request,
                                                    const nlohmann::json& candidate) const {
  ASSIGN_OR_RETURN(ParallaxTheme theme, ParseCandidate(api, request, candidate));
  return api.UpdateParallaxTheme(std::move(theme));
}

}  // namespace zebes
