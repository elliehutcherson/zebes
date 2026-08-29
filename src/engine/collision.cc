#include "engine/collision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/status_macros.h"
#include "objects/vec.h"

namespace zebes {
namespace {

struct Projection {
  double min = 0.0;
  double max = 0.0;
};

absl::Status ValidateBox(AxisAlignedBox box) {
  if (!IsFinite(box.min) || !IsFinite(box.max) || box.min.x >= box.max.x ||
      box.min.y >= box.max.y) {
    return absl::InvalidArgumentError("Collision box must have finite positive dimensions");
  }

  return absl::OkStatus();
}

absl::Status ValidateConvexPolygon(absl::Span<const Vec> polygon) {
  if (polygon.size() < 3) {
    return absl::InvalidArgumentError("Collision polygon needs at least three points");
  }

  double winding = 0.0;
  for (size_t index = 0; index < polygon.size(); ++index) {
    const Vec current = polygon[index];
    const Vec next = polygon[(index + 1) % polygon.size()];
    const Vec after_next = polygon[(index + 2) % polygon.size()];
    if (!IsFinite(current)) {
      return absl::InvalidArgumentError("Collision polygon contains a non-finite point");
    }

    const Vec edge = next - current;
    const Vec next_edge = after_next - next;
    if (std::hypot(edge.x, edge.y) == 0.0) {
      return absl::InvalidArgumentError("Collision polygon contains a zero-length edge");
    }

    const double cross = edge.x * next_edge.y - edge.y * next_edge.x;
    if (cross == 0.0) {
      return absl::InvalidArgumentError("Collision polygon contains collinear edges");
    }
    if (winding == 0.0) {
      winding = cross;
      continue;
    }
    if ((winding < 0.0) != (cross < 0.0)) {
      return absl::InvalidArgumentError("Collision polygon must be convex");
    }
  }

  return absl::OkStatus();
}

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

Vec EdgeNormal(absl::Span<const Vec> polygon, size_t index) {
  const Vec edge = polygon[(index + 1) % polygon.size()] - polygon[index];
  const double length = std::hypot(edge.x, edge.y);
  return {.x = -edge.y / length, .y = edge.x / length};
}

CollisionContact MinimumAxisSeparation(Projection box, Projection polygon, Vec axis) {
  const double negative_distance = box.max - polygon.min;
  const double positive_distance = polygon.max - box.min;
  if (negative_distance <= positive_distance) {
    return {
        .normal = axis * -1.0,
        .penetration = negative_distance,
    };
  }

  return {.normal = axis, .penetration = positive_distance};
}

}  // namespace

AxisAlignedBox TranslateBox(AxisAlignedBox box, Vec displacement) {
  return {.min = box.min + displacement, .max = box.max + displacement};
}

Vec RemoveInwardComponent(Vec vector, Vec outward_normal) {
  const double inward = Dot(vector, outward_normal);
  if (inward >= 0.0) return vector;
  return vector - outward_normal * inward;
}

absl::StatusOr<std::optional<CollisionContact>> IntersectBoxWithConvexPolygon(
    AxisAlignedBox box, absl::Span<const Vec> polygon) {
  RETURN_IF_ERROR(ValidateBox(box));
  RETURN_IF_ERROR(ValidateConvexPolygon(polygon));

  std::optional<CollisionContact> contact;
  const auto test_axis = [&](Vec axis) {
    const Projection box_projection = ProjectBox(box, axis);
    const Projection polygon_projection = ProjectPolygon(polygon, axis);
    if (box_projection.max <= polygon_projection.min ||
        polygon_projection.max <= box_projection.min) {
      return false;
    }

    const CollisionContact candidate =
        MinimumAxisSeparation(box_projection, polygon_projection, axis);
    if (!contact.has_value() || candidate.penetration < contact->penetration) {
      contact = candidate;
    }
    return true;
  };

  if (!test_axis({1.0, 0.0}) || !test_axis({0.0, 1.0})) return std::nullopt;
  for (size_t index = 0; index < polygon.size(); ++index) {
    if (!test_axis(EdgeNormal(polygon, index))) return std::nullopt;
  }

  return contact;
}

absl::StatusOr<std::optional<CollisionSweepContact>> SweepBoxWithConvexPolygon(
    AxisAlignedBox box, Vec displacement, absl::Span<const Vec> polygon) {
  RETURN_IF_ERROR(ValidateBox(box));
  RETURN_IF_ERROR(ValidateConvexPolygon(polygon));
  if (!IsFinite(displacement)) {
    return absl::InvalidArgumentError("Collision displacement must be finite");
  }

  ASSIGN_OR_RETURN(const std::optional<CollisionContact> initial_contact,
                   IntersectBoxWithConvexPolygon(box, polygon));
  if (initial_contact.has_value()) {
    return CollisionSweepContact{.time = 0.0, .normal = initial_contact->normal};
  }
  if (displacement == Vec{}) return std::nullopt;

  double latest_entry = -std::numeric_limits<double>::infinity();
  double earliest_exit = std::numeric_limits<double>::infinity();
  Vec impact_normal;
  const auto test_axis = [&](Vec axis) {
    const Projection box_projection = ProjectBox(box, axis);
    const Projection polygon_projection = ProjectPolygon(polygon, axis);
    const double axis_velocity = Dot(displacement, axis);
    if (axis_velocity == 0.0) {
      return box_projection.max > polygon_projection.min &&
             polygon_projection.max > box_projection.min;
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
      impact_normal = axis_velocity > 0.0 ? axis * -1.0 : axis;
    }
    earliest_exit = std::min(earliest_exit, axis_exit);
    return latest_entry < earliest_exit;
  };

  if (!test_axis({1.0, 0.0}) || !test_axis({0.0, 1.0})) return std::nullopt;
  for (size_t index = 0; index < polygon.size(); ++index) {
    if (!test_axis(EdgeNormal(polygon, index))) return std::nullopt;
  }

  const double impact_time = std::max(0.0, latest_entry);
  if (impact_time > 1.0 || impact_time >= earliest_exit) return std::nullopt;
  return CollisionSweepContact{.time = impact_time, .normal = impact_normal};
}

}  // namespace zebes
