#include "engine/tile_movement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "engine/tile_collision.h"
#include "objects/level.h"
#include "objects/tile_shape_geometry.h"
#include "objects/tileset.h"
#include "objects/vec.h"

namespace zebes {
namespace {

constexpr double kTimeTolerance = 1e-9;
constexpr double kNormalTolerance = 1e-12;
constexpr double kMotionTolerance = 1e-12;
constexpr int kMaximumResponseIterations = 8;

struct TileCellRange {
  int min_x = 0;
  int min_y = 0;
  int max_x = -1;
  int max_y = -1;

  bool empty() const { return min_x > max_x || min_y > max_y; }
};

struct ContactManifold {
  std::array<TileMovementContact, kMaxTileMovementContactCount> contacts;
  size_t count = 0;
  double time = 1.0;
};

bool IsFinite(Vec value) { return std::isfinite(value.x) && std::isfinite(value.y); }

double Dot(Vec left, Vec right) { return left.x * right.x + left.y * right.y; }

Vec Add(Vec left, Vec right) { return {.x = left.x + right.x, .y = left.y + right.y}; }

Vec Scale(Vec value, double scale) { return {.x = value.x * scale, .y = value.y * scale}; }

AxisAlignedBox Translate(AxisAlignedBox box, Vec displacement) {
  return {.min = Add(box.min, displacement), .max = Add(box.max, displacement)};
}

bool HasMotion(Vec displacement) {
  return std::abs(displacement.x) > kMotionTolerance || std::abs(displacement.y) > kMotionTolerance;
}

absl::Status ValidateOptions(const TileMovementOptions& options) {
  if (options.tile_width <= 0 || options.tile_height <= 0) {
    return absl::InvalidArgumentError("Tile movement dimensions must be positive");
  }
  if (!IsFinite(options.box.min) || !IsFinite(options.box.max) ||
      options.box.min.x >= options.box.max.x || options.box.min.y >= options.box.max.y) {
    return absl::InvalidArgumentError("Tile movement box must have finite positive dimensions");
  }
  if (!IsFinite(options.displacement) || !IsFinite(options.velocity)) {
    return absl::InvalidArgumentError("Tile movement vectors must be finite");
  }
  return absl::OkStatus();
}

absl::StatusOr<int> CheckedTileCoordinate(double tile_coordinate, const char* boundary) {
  if (!std::isfinite(tile_coordinate) ||
      tile_coordinate < static_cast<double>(std::numeric_limits<int>::min()) ||
      tile_coordinate > static_cast<double>(std::numeric_limits<int>::max())) {
    return absl::OutOfRangeError(
        absl::StrCat("Tile movement ", boundary, " coordinate exceeds the tile grid"));
  }
  return static_cast<int>(tile_coordinate);
}

absl::StatusOr<TileCellRange> SweptCellRange(AxisAlignedBox box, Vec displacement, int tile_width,
                                             int tile_height) {
  const AxisAlignedBox destination = Translate(box, displacement);
  const double min_x = std::min(box.min.x, destination.min.x);
  const double min_y = std::min(box.min.y, destination.min.y);
  const double max_x = std::max(box.max.x, destination.max.x);
  const double max_y = std::max(box.max.y, destination.max.y);
  if (max_x <= 0.0 || max_y <= 0.0) return TileCellRange{};

  ASSIGN_OR_RETURN(int first_x, CheckedTileCoordinate(std::floor(min_x / tile_width), "minimum X"));
  ASSIGN_OR_RETURN(int first_y,
                   CheckedTileCoordinate(std::floor(min_y / tile_height), "minimum Y"));
  // Include the cell beginning exactly at the destination maximum so a
  // time-of-impact at t=1 remains observable. Narrow phase rejects stationary
  // or tangential touching.
  ASSIGN_OR_RETURN(int last_x, CheckedTileCoordinate(std::floor(max_x / tile_width), "maximum X"));
  ASSIGN_OR_RETURN(int last_y, CheckedTileCoordinate(std::floor(max_y / tile_height), "maximum Y"));
  first_x = std::max(0, first_x);
  first_y = std::max(0, first_y);
  return TileCellRange{
      .min_x = first_x,
      .min_y = first_y,
      .max_x = last_x,
      .max_y = last_y,
  };
}

bool OneWayBlocks(const TileCollisionDefinition& definition, Vec displacement, Vec normal) {
  if (!definition.is_one_way) return true;
  return displacement.y > kMotionTolerance && normal.y < -kNormalTolerance &&
         Dot(displacement, normal) < -kMotionTolerance;
}

enum class TileBoundary { kLeft, kRight, kTop, kBottom };

struct SharedBoundary {
  int neighbor_x = 0;
  int neighbor_y = 0;
  TileBoundary current = TileBoundary::kLeft;
  TileBoundary neighbor = TileBoundary::kRight;
};

struct BoundaryInterval {
  double min = 0.0;
  double max = 0.0;
};

std::optional<SharedBoundary> BoundaryBehindNormal(Vec normal) {
  if (std::abs(normal.x + 1.0) <= kNormalTolerance && std::abs(normal.y) <= kNormalTolerance) {
    return SharedBoundary{
        .neighbor_x = -1,
        .current = TileBoundary::kLeft,
        .neighbor = TileBoundary::kRight,
    };
  }
  if (std::abs(normal.x - 1.0) <= kNormalTolerance && std::abs(normal.y) <= kNormalTolerance) {
    return SharedBoundary{
        .neighbor_x = 1,
        .current = TileBoundary::kRight,
        .neighbor = TileBoundary::kLeft,
    };
  }
  if (std::abs(normal.y + 1.0) <= kNormalTolerance && std::abs(normal.x) <= kNormalTolerance) {
    return SharedBoundary{
        .neighbor_y = -1,
        .current = TileBoundary::kTop,
        .neighbor = TileBoundary::kBottom,
    };
  }
  if (std::abs(normal.y - 1.0) <= kNormalTolerance && std::abs(normal.x) <= kNormalTolerance) {
    return SharedBoundary{
        .neighbor_y = 1,
        .current = TileBoundary::kBottom,
        .neighbor = TileBoundary::kTop,
    };
  }
  return std::nullopt;
}

std::optional<BoundaryInterval> ShapeBoundaryInterval(TileShape shape, TileBoundary boundary) {
  std::optional<BoundaryInterval> interval;
  for (const TilePoint point : TileShapePolygon(shape)) {
    const bool vertical = boundary == TileBoundary::kLeft || boundary == TileBoundary::kRight;
    const double boundary_coordinate =
        boundary == TileBoundary::kLeft || boundary == TileBoundary::kTop ? 0.0 : 1.0;
    const double perpendicular = vertical ? point.x : point.y;
    if (std::abs(perpendicular - boundary_coordinate) > kNormalTolerance) continue;

    const double along = vertical ? point.y : point.x;
    if (!interval.has_value()) {
      interval = {.min = along, .max = along};
      continue;
    }
    interval->min = std::min(interval->min, along);
    interval->max = std::max(interval->max, along);
  }
  return interval;
}

absl::StatusOr<bool> IsCoveredTileFace(const TileMovementOptions& options, int tile_x, int tile_y,
                                       TileShape shape, Vec normal) {
  const std::optional<SharedBoundary> boundary = BoundaryBehindNormal(normal);
  if (!boundary.has_value()) return false;
  const int neighbor_x = tile_x + boundary->neighbor_x;
  const int neighbor_y = tile_y + boundary->neighbor_y;
  if (neighbor_x < 0 || neighbor_y < 0) return false;
  ASSIGN_OR_RETURN(const int neighbor_id, GetTileAt(options.layer, neighbor_x, neighbor_y));
  if (neighbor_id == 0) return false;
  const auto neighbor = options.tiles.find(neighbor_id);
  if (neighbor == options.tiles.end()) {
    return absl::FailedPreconditionError(absl::StrCat("Tile layer references unknown tile ID ",
                                                      neighbor_id, " at (", neighbor_x, ", ",
                                                      neighbor_y, ")"));
  }
  if (neighbor->second.shape == TileShape::kNone || neighbor->second.is_one_way) return false;

  const std::optional<BoundaryInterval> current_interval =
      ShapeBoundaryInterval(shape, boundary->current);
  const std::optional<BoundaryInterval> neighbor_interval =
      ShapeBoundaryInterval(neighbor->second.shape, boundary->neighbor);
  if (!current_interval.has_value() || !neighbor_interval.has_value()) return false;
  return neighbor_interval->min <= current_interval->min + kNormalTolerance &&
         neighbor_interval->max >= current_interval->max - kNormalTolerance;
}

absl::Status AddContact(ContactManifold& manifold, const TileMovementContact& contact) {
  if (manifold.count >= manifold.contacts.size()) {
    return absl::ResourceExhaustedError("Tile movement simultaneous contact capacity exceeded");
  }
  manifold.contacts[manifold.count++] = contact;
  return absl::OkStatus();
}

absl::StatusOr<std::optional<ContactManifold>> FindEarliestContacts(
    const TileMovementOptions& options, AxisAlignedBox box, Vec displacement) {
  ASSIGN_OR_RETURN(const TileCellRange range,
                   SweptCellRange(box, displacement, options.tile_width, options.tile_height));
  if (range.empty()) return std::nullopt;

  ContactManifold manifold;
  bool found = false;
  for (int64_t tile_y = range.min_y; tile_y <= range.max_y; ++tile_y) {
    for (int64_t tile_x = range.min_x; tile_x <= range.max_x; ++tile_x) {
      const int cell_x = static_cast<int>(tile_x);
      const int cell_y = static_cast<int>(tile_y);
      ASSIGN_OR_RETURN(const int tile_id, GetTileAt(options.layer, cell_x, cell_y));
      if (tile_id == 0) continue;
      const auto definition = options.tiles.find(tile_id);
      if (definition == options.tiles.end()) {
        return absl::FailedPreconditionError(absl::StrCat(
            "Tile layer references unknown tile ID ", tile_id, " at (", cell_x, ", ", cell_y, ")"));
      }
      if (definition->second.shape == TileShape::kNone) continue;

      const Vec origin{
          .x = cell_x * static_cast<double>(options.tile_width),
          .y = cell_y * static_cast<double>(options.tile_height),
      };
      const Vec size{.x = static_cast<double>(options.tile_width),
                     .y = static_cast<double>(options.tile_height)};
      ASSIGN_OR_RETURN(const std::optional<TileCollisionContact> overlap,
                       IntersectBoxWithTileShape(box, definition->second.shape, origin, size));
      if (overlap.has_value()) {
        // One-way geometry only blocks a crossing from its supporting side.
        // Starting inside it cannot retroactively create a floor contact.
        if (definition->second.is_one_way) continue;
        return absl::FailedPreconditionError(absl::StrCat(
            "Moving box overlaps blocking tile ID ", tile_id, " at (", cell_x, ", ", cell_y, ")"));
      }

      ASSIGN_OR_RETURN(
          const std::optional<TileCollisionSweepContact> swept,
          SweepBoxWithTileShape(box, displacement, definition->second.shape, origin, size));
      if (!swept.has_value() || !OneWayBlocks(definition->second, displacement, swept->normal)) {
        continue;
      }
      ASSIGN_OR_RETURN(
          const bool covered,
          IsCoveredTileFace(options, cell_x, cell_y, definition->second.shape, swept->normal));
      if (covered) continue;

      const TileMovementContact contact{
          .tile_x = cell_x,
          .tile_y = cell_y,
          .tile_id = tile_id,
          .time = swept->time,
          .normal = swept->normal,
      };
      if (!found || swept->time < manifold.time - kTimeTolerance) {
        manifold = ContactManifold{.time = swept->time};
        RETURN_IF_ERROR(AddContact(manifold, contact));
        found = true;
        continue;
      }
      if (std::abs(swept->time - manifold.time) <= kTimeTolerance) {
        RETURN_IF_ERROR(AddContact(manifold, contact));
      }
    }
  }
  if (!found) return std::nullopt;
  return manifold;
}

void ProjectOutward(Vec normal, Vec& vector) {
  const double inward = Dot(vector, normal);
  if (inward < 0.0) vector = Add(vector, Scale(normal, -inward));
}

absl::Status AppendContacts(const ContactManifold& manifold, double global_time,
                            TileMovementResult& result) {
  for (size_t index = 0; index < manifold.count; ++index) {
    if (result.contact_count >= result.contacts.size()) {
      return absl::ResourceExhaustedError("Tile movement contact history capacity exceeded");
    }
    TileMovementContact contact = manifold.contacts[index];
    contact.time = global_time;
    result.contacts[result.contact_count++] = contact;
    result.grounded = result.grounded || contact.normal.y < -kNormalTolerance;
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<TileCollisionLookup> BuildTileCollisionLookup(const Tileset& tileset) {
  TileCollisionLookup lookup;
  lookup.reserve(tileset.tiles.size());
  for (const Tile& tile : tileset.tiles) {
    if (tile.id <= 0) {
      return absl::InvalidArgumentError("Collision tile IDs must be positive");
    }
    const int shape = static_cast<int>(tile.shape);
    if (shape < static_cast<int>(TileShape::kNone) ||
        shape > static_cast<int>(TileShape::kSteepSlopeCeilingTallLeftTop)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Collision tile ", tile.id, " has an invalid shape"));
    }
    if (tile.shape != TileShape::kNone && TileShapePolygon(tile.shape).empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Collision tile ", tile.id, " has no shape geometry"));
    }
    if (!lookup
             .emplace(tile.id,
                      TileCollisionDefinition{.shape = tile.shape, .is_one_way = tile.is_one_way})
             .second) {
      return absl::InvalidArgumentError(absl::StrCat("Duplicate collision tile ID: ", tile.id));
    }
  }
  return lookup;
}

absl::StatusOr<TileMovementResult> MoveBoxThroughTileLayer(const TileMovementOptions& options) {
  RETURN_IF_ERROR(ValidateOptions(options));
  TileMovementResult result{.box = options.box, .velocity = options.velocity};
  Vec remaining = options.displacement;
  double elapsed_time = 0.0;
  double remaining_time = 1.0;

  for (int iteration = 0; iteration < kMaximumResponseIterations; ++iteration) {
    ASSIGN_OR_RETURN(const std::optional<ContactManifold> manifold,
                     FindEarliestContacts(options, result.box, remaining));
    if (!manifold.has_value()) {
      result.box = Translate(result.box, remaining);
      return result;
    }

    result.box = Translate(result.box, Scale(remaining, manifold->time));
    const double global_time = elapsed_time + remaining_time * manifold->time;
    RETURN_IF_ERROR(AppendContacts(*manifold, global_time, result));

    remaining = Scale(remaining, 1.0 - manifold->time);
    for (size_t contact = 0; contact < manifold->count; ++contact) {
      ProjectOutward(manifold->contacts[contact].normal, remaining);
      ProjectOutward(manifold->contacts[contact].normal, result.velocity);
    }
    elapsed_time = global_time;
    remaining_time *= 1.0 - manifold->time;
    if (!HasMotion(remaining)) return result;
  }

  return absl::ResourceExhaustedError("Tile movement response iteration limit exceeded");
}

}  // namespace zebes
