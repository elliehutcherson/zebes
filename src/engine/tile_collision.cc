#include "engine/tile_collision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/status_macros.h"
#include "objects/tile_shape_geometry.h"
#include "objects/tileset.h"
#include "objects/vec.h"

namespace zebes {
namespace {

constexpr size_t kMaxTileShapePointCount = 4;
constexpr size_t kBoxAxisCount = 2;
constexpr size_t kMaxCollisionAxisCount = kBoxAxisCount + kMaxTileShapePointCount;

struct WorldPolygon {
  std::array<Vec, kMaxTileShapePointCount> points;
  size_t count = 0;
};

struct CollisionAxisList {
  std::array<Vec, kMaxCollisionAxisCount> axes;
  size_t count = 0;
};

bool IsFinite(Vec value) { return std::isfinite(value.x) && std::isfinite(value.y); }

double Dot(Vec left, Vec right) { return left.x * right.x + left.y * right.y; }

Vec Subtract(Vec left, Vec right) { return {.x = left.x - right.x, .y = left.y - right.y}; }

struct Projection {
  double min = 0.0;
  double max = 0.0;
};

Projection ProjectBox(AxisAlignedBox box, Vec axis) {
  const std::array<Vec, 4> corners = {{
      box.min,
      {.x = box.max.x, .y = box.min.y},
      box.max,
      {.x = box.min.x, .y = box.max.y},
  }};
  Projection projection{.min = Dot(corners.front(), axis), .max = Dot(corners.front(), axis)};
  for (const Vec corner : corners) {
    const double distance = Dot(corner, axis);
    projection.min = std::min(projection.min, distance);
    projection.max = std::max(projection.max, distance);
  }
  return projection;
}

Projection ProjectPolygon(absl::Span<const Vec> polygon, Vec axis) {
  Projection projection{.min = Dot(polygon.front(), axis), .max = Dot(polygon.front(), axis)};
  for (const Vec point : polygon) {
    const double distance = Dot(point, axis);
    projection.min = std::min(projection.min, distance);
    projection.max = std::max(projection.max, distance);
  }
  return projection;
}

CollisionAxisList CollisionAxes(absl::Span<const Vec> polygon) {
  CollisionAxisList axes;
  axes.axes[axes.count++] = {1.0, 0.0};
  axes.axes[axes.count++] = {0.0, 1.0};
  for (size_t index = 0; index < polygon.size(); ++index) {
    const Vec edge = Subtract(polygon[(index + 1) % polygon.size()], polygon[index]);
    const double length = std::hypot(edge.x, edge.y);
    axes.axes[axes.count++] = {.x = -edge.y / length, .y = edge.x / length};
  }
  return axes;
}

WorldPolygon ScaleTilePolygon(absl::Span<const TilePoint> normalized_polygon, Vec tile_origin,
                              Vec tile_size) {
  WorldPolygon polygon;
  for (const TilePoint point : normalized_polygon) {
    polygon.points[polygon.count++] = {
        .x = tile_origin.x + static_cast<double>(point.x) * tile_size.x,
        .y = tile_origin.y + static_cast<double>(point.y) * tile_size.y,
    };
  }
  return polygon;
}

absl::Status ValidatePolygon(const WorldPolygon& polygon) {
  if (polygon.count < 3 || polygon.count > kMaxTileShapePointCount) {
    return absl::InvalidArgumentError("Collision tile shape has invalid polygon point count");
  }
  for (size_t index = 0; index < polygon.count; ++index) {
    if (!IsFinite(polygon.points[index])) {
      return absl::InvalidArgumentError("Collision tile shape has non-finite geometry");
    }
    const Vec next = polygon.points[(index + 1) % polygon.count];
    const Vec edge = Subtract(next, polygon.points[index]);
    if (!IsFinite(edge) || std::hypot(edge.x, edge.y) == 0.0) {
      return absl::InvalidArgumentError("Collision tile shape has degenerate geometry");
    }
  }
  return absl::OkStatus();
}

TileCollisionContact MinimumAxisSeparation(Projection box, Projection polygon, Vec axis) {
  const double negative_distance = box.max - polygon.min;
  const double positive_distance = polygon.max - box.min;
  if (negative_distance <= positive_distance) {
    return {
        .normal = {.x = -axis.x, .y = -axis.y},
        .penetration = negative_distance,
    };
  }
  return {.normal = axis, .penetration = positive_distance};
}

absl::Status ValidateGeometry(AxisAlignedBox box, TileShape shape, Vec tile_origin, Vec tile_size) {
  if (!IsFinite(box.min) || !IsFinite(box.max) || box.min.x >= box.max.x ||
      box.min.y >= box.max.y) {
    return absl::InvalidArgumentError("Collision box must have finite positive dimensions");
  }
  if (!IsFinite(tile_origin) || !IsFinite(tile_size) || tile_size.x <= 0.0 || tile_size.y <= 0.0) {
    return absl::InvalidArgumentError("Collision tile must have finite positive dimensions");
  }
  const int shape_value = static_cast<int>(shape);
  if (shape_value < static_cast<int>(TileShape::kNone) ||
      shape_value > static_cast<int>(TileShape::kSteepSlopeCeilingTallLeftTop)) {
    return absl::InvalidArgumentError("Collision tile shape is invalid");
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::optional<TileCollisionContact>> IntersectBoxWithTileShape(AxisAlignedBox box,
                                                                              TileShape shape,
                                                                              Vec tile_origin,
                                                                              Vec tile_size) {
  RETURN_IF_ERROR(ValidateGeometry(box, shape, tile_origin, tile_size));
  if (shape == TileShape::kNone) return std::nullopt;

  const absl::Span<const TilePoint> normalized_polygon = TileShapePolygon(shape);
  if (normalized_polygon.empty()) {
    return absl::InvalidArgumentError("Collision tile shape has no geometry");
  }
  if (normalized_polygon.size() > kMaxTileShapePointCount) {
    return absl::InternalError("Collision tile shape exceeds the fixed polygon capacity");
  }

  const WorldPolygon world_polygon = ScaleTilePolygon(normalized_polygon, tile_origin, tile_size);
  RETURN_IF_ERROR(ValidatePolygon(world_polygon));
  const absl::Span<const Vec> polygon(world_polygon.points.data(), world_polygon.count);
  const CollisionAxisList collision_axes = CollisionAxes(polygon);
  const absl::Span<const Vec> axes(collision_axes.axes.data(), collision_axes.count);
  std::optional<TileCollisionContact> contact;

  for (const Vec axis : axes) {
    const Projection box_projection = ProjectBox(box, axis);
    const Projection polygon_projection = ProjectPolygon(polygon, axis);
    if (box_projection.max <= polygon_projection.min ||
        polygon_projection.max <= box_projection.min) {
      return std::nullopt;
    }
    const TileCollisionContact candidate =
        MinimumAxisSeparation(box_projection, polygon_projection, axis);
    if (!contact.has_value() || candidate.penetration < contact->penetration) {
      contact = candidate;
    }
  }

  return contact;
}

absl::StatusOr<std::optional<TileCollisionSweepContact>> SweepBoxWithTileShape(
    AxisAlignedBox box, Vec displacement, TileShape shape, Vec tile_origin, Vec tile_size) {
  RETURN_IF_ERROR(ValidateGeometry(box, shape, tile_origin, tile_size));
  if (!IsFinite(displacement)) {
    return absl::InvalidArgumentError("Collision displacement must be finite");
  }
  if (shape == TileShape::kNone) return std::nullopt;

  // Positive initial overlap is intentionally handled by the static primitive
  // so both APIs use the same minimum-translation and tie-breaking rule.
  ASSIGN_OR_RETURN(const std::optional<TileCollisionContact> initial_contact,
                   IntersectBoxWithTileShape(box, shape, tile_origin, tile_size));
  if (initial_contact.has_value()) {
    return TileCollisionSweepContact{.time = 0.0, .normal = initial_contact->normal};
  }
  if (displacement.x == 0.0 && displacement.y == 0.0) return std::nullopt;

  const absl::Span<const TilePoint> normalized_polygon = TileShapePolygon(shape);
  if (normalized_polygon.empty()) {
    return absl::InvalidArgumentError("Collision tile shape has no geometry");
  }
  const WorldPolygon world_polygon = ScaleTilePolygon(normalized_polygon, tile_origin, tile_size);
  RETURN_IF_ERROR(ValidatePolygon(world_polygon));
  const absl::Span<const Vec> polygon(world_polygon.points.data(), world_polygon.count);
  const CollisionAxisList collision_axes = CollisionAxes(polygon);
  const absl::Span<const Vec> axes(collision_axes.axes.data(), collision_axes.count);

  double latest_entry = -std::numeric_limits<double>::infinity();
  double earliest_exit = std::numeric_limits<double>::infinity();
  Vec impact_normal;
  for (const Vec axis : axes) {
    const Projection box_projection = ProjectBox(box, axis);
    const Projection polygon_projection = ProjectPolygon(polygon, axis);
    const double axis_velocity = Dot(displacement, axis);
    if (axis_velocity == 0.0) {
      if (box_projection.max <= polygon_projection.min ||
          polygon_projection.max <= box_projection.min) {
        return std::nullopt;
      }
      continue;
    }

    double axis_entry = 0.0;
    double axis_exit = 0.0;
    if (axis_velocity > 0.0) {
      axis_entry = (polygon_projection.min - box_projection.max) / axis_velocity;
      axis_exit = (polygon_projection.max - box_projection.min) / axis_velocity;
    } else {
      axis_entry = (polygon_projection.max - box_projection.min) / axis_velocity;
      axis_exit = (polygon_projection.min - box_projection.max) / axis_velocity;
    }
    if (axis_entry > latest_entry) {
      latest_entry = axis_entry;
      // The first axis owns an exact tie. This makes corner contacts stable
      // and independent of container or polygon traversal order.
      impact_normal = axis_velocity > 0.0 ? Vec{.x = -axis.x, .y = -axis.y} : axis;
    }
    earliest_exit = std::min(earliest_exit, axis_exit);
    if (latest_entry >= earliest_exit) return std::nullopt;
  }

  const double impact_time = std::max(0.0, latest_entry);
  if (impact_time > 1.0 || impact_time >= earliest_exit) return std::nullopt;
  return TileCollisionSweepContact{.time = impact_time, .normal = impact_normal};
}

}  // namespace zebes
