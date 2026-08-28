#include "curation/level_review_route.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

constexpr size_t kMaximumRouteSamples = 512;
constexpr double kCoordinateEpsilon = 1e-9;

struct RouteSource {
  std::string id;
  int zone_id = -1;
  std::string zone_name;
  Vec min;
  Vec max;
  bool horizontal = true;
};

struct CandidateCoordinate {
  double value = 0.0;
  std::vector<std::string> key_roles;
};

bool ContainsHalfOpen(double value, double minimum, double maximum) {
  return value >= minimum && value < maximum;
}

void AddCandidate(std::vector<CandidateCoordinate>& candidates, double value,
                  std::string key_role = {}) {
  CandidateCoordinate candidate{.value = value};
  if (!key_role.empty()) candidate.key_roles.push_back(std::move(key_role));
  candidates.push_back(std::move(candidate));
}

void AddRole(std::vector<std::string>& roles, const std::string& role) {
  if (role.empty() || std::find(roles.begin(), roles.end(), role) != roles.end()) return;
  roles.push_back(role);
}

std::vector<CandidateCoordinate> MergeCandidates(std::vector<CandidateCoordinate> candidates,
                                                 double minimum, double maximum) {
  std::erase_if(candidates, [minimum, maximum](const CandidateCoordinate& candidate) {
    return !std::isfinite(candidate.value) || candidate.value < minimum - kCoordinateEpsilon ||
           candidate.value > maximum + kCoordinateEpsilon;
  });
  for (CandidateCoordinate& candidate : candidates) {
    candidate.value = std::clamp(candidate.value, minimum, maximum);
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const CandidateCoordinate& left, const CandidateCoordinate& right) {
                     return left.value < right.value;
                   });

  std::vector<CandidateCoordinate> merged;
  for (CandidateCoordinate& candidate : candidates) {
    if (merged.empty() || std::abs(candidate.value - merged.back().value) > kCoordinateEpsilon) {
      merged.push_back(std::move(candidate));
      continue;
    }
    for (const std::string& role : candidate.key_roles) {
      AddRole(merged.back().key_roles, role);
    }
  }
  return merged;
}

void AddHorizontalFadeCandidates(const Level& level, const RouteSource& source, double route_y,
                                 std::vector<CandidateCoordinate>& candidates) {
  if (source.zone_id < 0) return;
  const ParallaxZone* source_zone = FindParallaxZoneById(level.zones, source.zone_id);
  if (source_zone == nullptr) return;

  for (const ParallaxZone& other : level.zones) {
    if (other.id == source.zone_id) continue;
    const bool source_left = source_zone->max_point.x == other.min_point.x;
    const bool other_left = other.max_point.x == source_zone->min_point.x;
    if (!source_left && !other_left) continue;
    const double projection_min = std::max(source_zone->min_point.y, other.min_point.y);
    const double projection_max = std::min(source_zone->max_point.y, other.max_point.y);
    if (!ContainsHalfOpen(route_y, projection_min, projection_max)) continue;

    const ParallaxZone& left = source_left ? *source_zone : other;
    const ParallaxZone& right = source_left ? other : *source_zone;
    const double edge = left.max_point.x;
    const double start = edge - left.fade_length.x;
    const double end = edge + right.fade_length.x;
    if (start == end) continue;
    AddCandidate(candidates, start, "fade-start");
    AddCandidate(candidates, (start + end) * 0.5, "fade-middle");
    AddCandidate(candidates, edge, "fade-boundary");
    AddCandidate(candidates, end, "fade-end");
  }
}

void AddVerticalFadeCandidates(const Level& level, const RouteSource& source, double route_x,
                               std::vector<CandidateCoordinate>& candidates) {
  if (source.zone_id < 0) return;
  const ParallaxZone* source_zone = FindParallaxZoneById(level.zones, source.zone_id);
  if (source_zone == nullptr) return;

  for (const ParallaxZone& other : level.zones) {
    if (other.id == source.zone_id) continue;
    const bool source_top = source_zone->max_point.y == other.min_point.y;
    const bool other_top = other.max_point.y == source_zone->min_point.y;
    if (!source_top && !other_top) continue;
    const double projection_min = std::max(source_zone->min_point.x, other.min_point.x);
    const double projection_max = std::min(source_zone->max_point.x, other.max_point.x);
    if (!ContainsHalfOpen(route_x, projection_min, projection_max)) continue;

    const ParallaxZone& top = source_top ? *source_zone : other;
    const ParallaxZone& bottom = source_top ? other : *source_zone;
    const double edge = top.max_point.y;
    const double start = edge - top.fade_length.y;
    const double end = edge + bottom.fade_length.y;
    if (start == end) continue;
    AddCandidate(candidates, start, "fade-start");
    AddCandidate(candidates, (start + end) * 0.5, "fade-middle");
    AddCandidate(candidates, edge, "fade-boundary");
    AddCandidate(candidates, end, "fade-end");
  }
}

std::vector<RouteSource> BuildRouteSources(const Level& level) {
  std::vector<RouteSource> sources;
  sources.reserve(std::max<size_t>(1, level.zones.size()));
  for (const ParallaxZone& zone : level.zones) {
    const double width = zone.max_point.x - zone.min_point.x;
    const double height = zone.max_point.y - zone.min_point.y;
    sources.push_back({
        .id = absl::StrFormat("zone-%03d", zone.id),
        .zone_id = zone.id,
        .zone_name = zone.name,
        .min = zone.min_point,
        .max = zone.max_point,
        .horizontal = width >= height,
    });
  }
  if (!sources.empty()) return sources;

  sources.push_back({
      .id = "world",
      .zone_name = "Unzoned world",
      .min = {0.0, 0.0},
      .max = {level.width, level.height},
      .horizontal = level.width >= level.height,
  });
  return sources;
}

std::vector<double> CollectSecondaryProbes(const Level& level, const RouteSource& source) {
  std::vector<double> probes;
  auto add_point = [&source, &probes](Vec point) {
    if (!ContainsHalfOpen(point.x, source.min.x, source.max.x) ||
        !ContainsHalfOpen(point.y, source.min.y, source.max.y)) {
      return;
    }
    probes.push_back(source.horizontal ? point.y : point.x);
  };
  add_point(level.spawn_point);
  for (const WorldLayer& layer : level.layers) {
    for (const auto& [unused, entity] : layer.entities) {
      static_cast<void>(unused);
      if (entity.active) add_point(entity.transform.position);
    }
    for (const auto& [key, chunk] : layer.tile_chunks) {
      const TileChunkCoordinate coordinate = DecodeChunkKey(key);
      for (int index = 0; index < TileChunk::kSize * TileChunk::kSize; ++index) {
        if (chunk.tiles[index] == 0) continue;
        const int64_t tile_x =
            static_cast<int64_t>(coordinate.x) * TileChunk::kSize + index % TileChunk::kSize;
        const int64_t tile_y =
            static_cast<int64_t>(coordinate.y) * TileChunk::kSize + index / TileChunk::kSize;
        add_point(
            {(tile_x + 0.5) * level.tile_render_width, (tile_y + 0.5) * level.tile_render_height});
      }
    }
  }
  if (probes.empty()) {
    probes.push_back(source.horizontal ? (source.min.y + source.max.y) * 0.5
                                       : (source.min.x + source.max.x) * 0.5);
  }
  std::sort(probes.begin(), probes.end());
  probes.erase(std::unique(probes.begin(), probes.end()), probes.end());
  return probes;
}

std::vector<double> PlanSecondaryCenters(const std::vector<double>& probes, double half_view,
                                         double reachable_minimum, double reachable_maximum) {
  std::vector<double> centers;
  size_t first = 0;
  while (first < probes.size()) {
    size_t last = first;
    while (last + 1 < probes.size() && probes[last + 1] - probes[first] <= 2.0 * half_view) {
      ++last;
    }
    const double center =
        std::clamp((probes[first] + probes[last]) * 0.5, reachable_minimum, reachable_maximum);
    if (centers.empty() || center != centers.back()) centers.push_back(center);
    first = last + 1;
  }
  return centers;
}

absl::StatusOr<LevelReviewRoute> PlanRoute(const Level& level, const GameViewSize& game_view,
                                           const RouteSource& source, double zoom,
                                           double secondary_center, int track_index) {
  const double half_width = game_view.width / (2.0 * zoom);
  const double half_height = game_view.height / (2.0 * zoom);
  if (half_width * 2.0 > level.width || half_height * 2.0 > level.height) {
    const double minimum_zoom =
        std::max(game_view.width / level.width, game_view.height / level.height);
    return absl::FailedPreconditionError(
        absl::StrCat("game view does not fit inside level at zoom ", zoom,
                     "; minimum viable zoom is ", minimum_zoom));
  }

  const CameraCenterRoute reachable_world{
      .min = {half_width, half_height},
      .max = {level.width - half_width, level.height - half_height},
  };
  CameraCenterRoute centerline;
  if (source.horizontal) {
    centerline = {.min = {source.min.x, secondary_center}, .max = {source.max.x, secondary_center}};
  } else {
    centerline = {.min = {secondary_center, source.min.y}, .max = {secondary_center, source.max.y}};
  }

  ASSIGN_OR_RETURN(const CameraCenterRoute resolved,
                   ResolveCameraCenterRoute(
                       centerline, game_view, zoom,
                       CameraWorldBounds{.min = {0.0, 0.0}, .max = {level.width, level.height}}));
  const double minimum = source.horizontal ? resolved.min.x : resolved.min.y;
  const double maximum = source.horizontal ? resolved.max.x : resolved.max.y;
  const double visible_extent = (source.horizontal ? game_view.width : game_view.height) / zoom;
  // The first track carries seam-review evidence and therefore overlaps each
  // adjacent frame by at least half. Additional tracks exist to cover content
  // on the secondary axis; edge-to-edge sampling avoids duplicating the same
  // horizontal seam evidence for every floor or prop band.
  const double maximum_step = visible_extent * (track_index == 0 ? 0.5 : 1.0);
  const double length = maximum - minimum;
  const int segments =
      length == 0.0 ? 0 : std::max(1, static_cast<int>(std::ceil(length / maximum_step)));

  std::vector<CandidateCoordinate> candidates;
  candidates.reserve(static_cast<size_t>(segments) + 8);
  if (segments == 0) {
    AddCandidate(candidates, minimum, "start");
    AddCandidate(candidates, minimum, "middle");
    AddCandidate(candidates, minimum, "end");
  } else {
    for (int index = 0; index <= segments; ++index) {
      AddCandidate(candidates, minimum + length * index / segments);
    }
    AddCandidate(candidates, minimum, "start");
    AddCandidate(candidates, (minimum + maximum) * 0.5, "middle");
    AddCandidate(candidates, maximum, "end");
  }
  if (source.horizontal) {
    AddHorizontalFadeCandidates(level, source, resolved.min.y, candidates);
  } else {
    AddVerticalFadeCandidates(level, source, resolved.min.x, candidates);
  }
  candidates = MergeCandidates(std::move(candidates), minimum, maximum);

  LevelReviewRoute route{
      .id = absl::StrFormat("%s-track-%02d", source.id, track_index),
      .zone_id = source.zone_id,
      .zone_name = source.zone_name,
      .track_index = track_index,
      .horizontal = source.horizontal,
      .zoom = zoom,
      .centers = resolved,
  };
  route.samples.reserve(candidates.size());
  for (size_t index = 0; index < candidates.size(); ++index) {
    const CandidateCoordinate& candidate = candidates[index];
    const double progress = length == 0.0 ? 0.0 : (candidate.value - minimum) / length;
    const Vec center = source.horizontal ? Vec{candidate.value, resolved.min.y}
                                         : Vec{resolved.min.x, candidate.value};
    route.samples.push_back({
        .id = absl::StrFormat("frame-%04d", static_cast<int>(index)),
        .progress = progress,
        .camera = {.position = center,
                   .zoom = zoom,
                   .viewport_width = game_view.width,
                   .viewport_height = game_view.height},
        .key_roles = candidate.key_roles,
    });
  }
  return route;
}

}  // namespace

absl::StatusOr<std::vector<LevelReviewRoute>> PlanLevelReviewRoutes(
    const Level& level, const GameViewSize& game_view, absl::Span<const double> zooms) {
  RETURN_IF_ERROR(ValidateLevel(level));
  if (level.width <= 0.0 || level.height <= 0.0 || !game_view.IsValid() || zooms.empty()) {
    return absl::InvalidArgumentError(
        "level review routes require positive world, game-view, and zoom inputs");
  }
  for (size_t index = 0; index < zooms.size(); ++index) {
    if (!std::isfinite(zooms[index]) || zooms[index] <= 0.0 ||
        (index > 0 && zooms[index - 1] >= zooms[index])) {
      return absl::InvalidArgumentError("level review zooms must be positive and increasing");
    }
  }

  const std::vector<RouteSource> sources = BuildRouteSources(level);
  std::vector<std::vector<double>> probes_by_source;
  probes_by_source.reserve(sources.size());
  for (const RouteSource& source : sources) {
    probes_by_source.push_back(CollectSecondaryProbes(level, source));
  }
  std::vector<LevelReviewRoute> routes;
  routes.reserve(sources.size() * zooms.size());
  size_t sample_count = 0;
  for (const double zoom : zooms) {
    for (size_t source_index = 0; source_index < sources.size(); ++source_index) {
      const RouteSource& source = sources[source_index];
      const double half_secondary =
          (source.horizontal ? game_view.height : game_view.width) / (2.0 * zoom);
      const double secondary_world_extent = source.horizontal ? level.height : level.width;
      if (half_secondary * 2.0 > secondary_world_extent) {
        const double minimum_zoom =
            (source.horizontal ? game_view.height : game_view.width) / secondary_world_extent;
        return absl::FailedPreconditionError(
            absl::StrCat("game view does not fit inside level at zoom ", zoom,
                         "; minimum viable zoom is ", minimum_zoom));
      }
      const std::vector<double> centers =
          PlanSecondaryCenters(probes_by_source[source_index], half_secondary, half_secondary,
                               secondary_world_extent - half_secondary);
      for (size_t track_index = 0; track_index < centers.size(); ++track_index) {
        ASSIGN_OR_RETURN(LevelReviewRoute route,
                         PlanRoute(level, game_view, source, zoom, centers[track_index],
                                   static_cast<int>(track_index)));
        sample_count += route.samples.size();
        if (sample_count > kMaximumRouteSamples) {
          return absl::ResourceExhaustedError(
              absl::StrCat("level review route exceeds ", kMaximumRouteSamples, " camera samples"));
        }
        routes.push_back(std::move(route));
      }
    }
  }
  return routes;
}

}  // namespace zebes
