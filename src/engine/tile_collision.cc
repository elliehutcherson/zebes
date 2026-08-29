#include "engine/tile_collision.h"

#include <array>
#include <cstddef>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/status_macros.h"
#include "engine/collision.h"
#include "objects/tile_shape_geometry.h"
#include "objects/tileset.h"
#include "objects/vec.h"

namespace zebes {
namespace {

constexpr size_t kMaxTileShapePointCount = 4;

struct WorldPolygon {
  std::array<Vec, kMaxTileShapePointCount> points;
  size_t count = 0;
};

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

absl::StatusOr<WorldPolygon> BuildWorldPolygon(TileShape shape, Vec tile_origin, Vec tile_size) {
  const absl::Span<const TilePoint> normalized_polygon = TileShapePolygon(shape);
  if (normalized_polygon.empty()) {
    return absl::InvalidArgumentError("Collision tile shape has no geometry");
  }
  if (normalized_polygon.size() > kMaxTileShapePointCount) {
    return absl::InternalError("Collision tile shape exceeds the fixed polygon capacity");
  }

  WorldPolygon polygon;
  for (const TilePoint point : normalized_polygon) {
    polygon.points[polygon.count++] = {
        .x = tile_origin.x + static_cast<double>(point.x) * tile_size.x,
        .y = tile_origin.y + static_cast<double>(point.y) * tile_size.y,
    };
  }

  return polygon;
}

}  // namespace

absl::StatusOr<std::optional<TileCollisionContact>> IntersectBoxWithTileShape(AxisAlignedBox box,
                                                                              TileShape shape,
                                                                              Vec tile_origin,
                                                                              Vec tile_size) {
  RETURN_IF_ERROR(ValidateGeometry(box, shape, tile_origin, tile_size));
  if (shape == TileShape::kNone) return std::nullopt;

  ASSIGN_OR_RETURN(const WorldPolygon polygon, BuildWorldPolygon(shape, tile_origin, tile_size));
  return IntersectBoxWithConvexPolygon(box,
                                       absl::Span<const Vec>(polygon.points.data(), polygon.count));
}

absl::StatusOr<std::optional<TileCollisionSweepContact>> SweepBoxWithTileShape(
    AxisAlignedBox box, Vec displacement, TileShape shape, Vec tile_origin, Vec tile_size) {
  RETURN_IF_ERROR(ValidateGeometry(box, shape, tile_origin, tile_size));
  if (!IsFinite(displacement)) {
    return absl::InvalidArgumentError("Collision displacement must be finite");
  }
  if (shape == TileShape::kNone) return std::nullopt;

  ASSIGN_OR_RETURN(const WorldPolygon polygon, BuildWorldPolygon(shape, tile_origin, tile_size));
  return SweepBoxWithConvexPolygon(box, displacement,
                                   absl::Span<const Vec>(polygon.points.data(), polygon.count));
}

}  // namespace zebes
