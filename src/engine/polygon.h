#pragma once

#include <cmath>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "engine/vector.h"

namespace zebes {

enum AxisDirection : uint8_t { axis_none = 0, axis_left = 1, axis_right = 2 };

struct AxisOverlap {
  bool is_primary = false;
  AxisDirection primary_axis_direction = AxisDirection::axis_none;
  Vector axis;

  double left_distance = 0;
  double right_distance = 0;

  double left_magitude() const { return abs(left_distance); }
  double right_magitude() const { return abs(right_distance); }

  double min_magnitude() const {
    return left_magitude() < right_magitude() ? left_magitude()
                                              : right_magitude();
  }
  double max_magnitude() const {
    return left_magitude() > right_magitude() ? left_magitude()
                                              : right_magitude();
  }
  double primary_magitude() const {
    if (primary_axis_direction == AxisDirection::axis_left)
      return left_magitude();
    if (primary_axis_direction == AxisDirection::axis_right)
      return right_magitude();
    return min_magnitude();
  }

  double min_distance() const {
    return left_magitude() < right_magitude() ? left_distance : right_distance;
  }
  double max_distance() const {
    return left_magitude() > right_magitude() ? left_distance : right_distance;
  }
  double primary_distance() const {
    if (primary_axis_direction == AxisDirection::axis_left)
      return left_distance;
    if (primary_axis_direction == AxisDirection::axis_right)
      return right_distance;
    return min_distance();
  }

  double left_overlap_x() const { return axis.x * left_distance; }
  double left_overlap_y() const { return axis.y * left_distance; }

  double right_overlap_x() const { return axis.x * right_distance; }
  double right_overlap_y() const { return axis.y * right_distance; }

  double min_overlap_x() const { return axis.x * min_distance(); }
  double min_overlap_y() const { return axis.y * min_distance(); }

  double max_overlap_x() const { return axis.x * max_distance(); }
  double max_overlap_y() const { return axis.y * max_distance(); }

  double primary_overlap_x() const {
    if (primary_axis_direction == AxisDirection::axis_left)
      return left_overlap_x();
    if (primary_axis_direction == AxisDirection::axis_right)
      return right_overlap_x();
    return min_overlap_x();
  }

  double primary_overlap_y() const {
    if (primary_axis_direction == AxisDirection::axis_left)
      return left_overlap_y();
    if (primary_axis_direction == AxisDirection::axis_right)
      return right_overlap_y();
    return min_overlap_y();
  }

  std::string ToString() const {
    std::string message = absl::StrFormat("is_primary: %v\n", is_primary);
    absl::StrAppend(&message, absl::StrFormat("axis: %s\n", axis.ToString()));
    absl::StrAppend(&message,
                    absl::StrFormat("min_magnitude: %f\n", min_magnitude()));
    absl::StrAppend(&message,
                    absl::StrFormat("min_overlap_x: %f\n", min_overlap_x()));
    absl::StrAppend(&message,
                    absl::StrFormat("min_overlap_y: %f\n", min_overlap_y()));
    absl::StrAppend(&message,
                    absl::StrFormat("max_magnitude: %f\n", max_magnitude()));
    absl::StrAppend(&message,
                    absl::StrFormat("max_overlap_x: %f\n", max_overlap_x()));
    absl::StrAppend(&message,
                    absl::StrFormat("max_overlap_y: %f\n", max_overlap_y()));
    return message;
  }
};

struct PolygonOverlap {
  bool overlap = false;
  bool has_primary = false;
  int min_overlap_index = -1;
  int min_primary_overlap_index = -1;
  std::vector<AxisOverlap> axis_overlaps;

  int min_overlap_distance() const {
    if (min_overlap_index < 0)
      return 1000000;
    return axis_overlaps[min_overlap_index].min_distance();
  }
  int min_overlap_magnitude() const { return abs(min_overlap_distance()); }
  double min_overlap_x() const {
    if (min_overlap_index < 0)
      abort();
    const AxisOverlap &axis_overlap = axis_overlaps[min_overlap_index];
    return axis_overlap.min_overlap_x();
  }
  double min_overlap_y() const {
    if (min_overlap_index < 0)
      abort();
    const AxisOverlap &axis_overlap = axis_overlaps[min_overlap_index];
    return axis_overlap.min_overlap_y();
  }

  int min_primary_overlap_distance() const {
    if (min_primary_overlap_index < 0)
      return 1000000;
    return axis_overlaps[min_primary_overlap_index].primary_distance();
  }
  int min_primary_overlap_magnitude() const {
    if (min_primary_overlap_index < 0)
      return 1000000;
    return axis_overlaps[min_primary_overlap_index].primary_magitude();
  }
  double min_primary_overlap_x() const {
    if (min_primary_overlap_index < 0)
      abort();
    const AxisOverlap &axis_overlap = axis_overlaps[min_primary_overlap_index];
    return axis_overlap.primary_overlap_x();
  }
  double min_primary_overlap_y() const {
    if (min_primary_overlap_index < 0)
      abort();
    const AxisOverlap &axis_overlap = axis_overlaps[min_primary_overlap_index];
    return axis_overlap.primary_overlap_y();
  }

  void AddAxisOverlap(AxisOverlap axis_overlap) {
    if (axis_overlap.is_primary &&
        min_primary_overlap_magnitude() > axis_overlap.primary_magitude()) {
      has_primary = true;
      min_primary_overlap_index = axis_overlaps.size();
    }
    if (min_overlap_magnitude() > axis_overlap.min_magnitude()) {
      min_overlap_index = axis_overlaps.size();
    }
    axis_overlaps.push_back(axis_overlap);
  }

  std::string ToString() const {
    std::string message = absl::StrFormat("has_primary: %v\n", has_primary);
    absl::StrAppend(&message, absl::StrFormat("min_overlap_index: %d\n",
                                              min_overlap_index));
    absl::StrAppend(&message, absl::StrFormat("min_primary_overlap_index: %d\n",
                                              min_primary_overlap_index));
    absl::StrAppend(&message, absl::StrFormat("axis_overlaps.size(): %d\n",
                                              axis_overlaps.size()));
    absl::StrAppend(&message, absl::StrFormat("min_overlap_distance: %f\n",
                                              min_overlap_distance()));
    absl::StrAppend(&message,
                    absl::StrFormat("min_overlap_x: %f\n", min_overlap_x()));
    absl::StrAppend(&message,
                    absl::StrFormat("min_overlap_y: %f\n", min_overlap_y()));
    if (has_primary) {
      const AxisOverlap &primary_axis_overlap =
          axis_overlaps[min_primary_overlap_index];
      absl::StrAppend(&message,
                      absl::StrFormat("min_primary_overlap_distance: %f\n",
                                      min_primary_overlap_distance()));
      absl::StrAppend(&message, absl::StrFormat("min_primary_overlap_x: %f\n",
                                                min_primary_overlap_x()));
      absl::StrAppend(&message, absl::StrFormat("min_primary_overlap_y: %f\n",
                                                min_primary_overlap_y()));
      absl::StrAppend(
          &message,
          absl::StrFormat("min_primary_axis_direction: %d\n",
                          primary_axis_overlap.primary_axis_direction));
    }
    int axis_index = 0;
    for (const AxisOverlap &axis_overlap : axis_overlaps) {
      absl::StrAppend(&message,
                      absl::StrFormat("axis_index: %d\n", axis_index++));
      absl::StrAppend(&message, axis_overlap.ToString());
    }
    return message;
  }
};

class Polygon {
public:
  Polygon(std::vector<Point> vertices) : vertices_(vertices) {
    float x_min = 1000000;
    float y_min = 1000000;
    float x_max = -1000000;
    float y_max = -1000000;
    for (int i = 0; i < vertices.size(); i++) {
      const Point &p = vertices_[i];
      if (p.x < x_min) {
        x_min = p.x;
        x_min_index_ = i;
      }
      if (p.y < x_min) {
        y_min = p.y;
        y_min_index_ = i;
      }
      if (p.x > x_max) {
        x_max = p.x;
        x_max_index_ = i;
      }
      if (p.y > y_max) {
        y_max = p.y;
        y_max_index_ = i;
      }
    }
  }
  ~Polygon() = default;
  // Get vertices
  const std::vector<Point> *vertices() const;
  // Get the min x value from all vertices.
  float x_min() const;
  // Get the min x floor value from all vertices.
  int x_min_floor() const;
  // Get the max x value from all vertices.
  float x_max() const;
  // Get the max x floor value from all vertices.
  int x_max_floor() const;
  // Get the min y value from all vertices.
  float y_min() const;
  // Get the min y floor value from all vertices.
  int y_min_floor() const;
  // Get the max y value from all vertices.
  float y_max() const;
  // Get the max y floor value from all vertices.
  int y_max_floor() const;
  // Get debug info include list of vertices.
  std::string GetDebugString() const;
  // Get project axes.
  std::vector<Vector> GetAxes() const;
  // Get primary axes indices.
  const absl::flat_hash_map<uint8_t, AxisDirection> *GetPrimaryAxes() const;
  // Get overlap for all vectors on the specified axis.
  std::optional<AxisOverlap> GetOverlapOnAxis(const Polygon &other,
                                              const Vector &axis) const;
  // Return whether other polygon overlaps.
  PolygonOverlap GetOverlap(const Polygon &other) const;
  // Add primary axis index. This axis will be marked as a primary axis
  // when caculating the overlap between two objects.
  absl::Status
  AddPrimaryAxisIndex(uint8_t index,
                      AxisDirection axis_direction = AxisDirection::axis_none);
  // Move all vertices.
  void Move(float x, float y);

private:
  std::vector<Point> vertices_;
  uint8_t x_min_index_ = 0;
  uint8_t x_max_index_ = 0;
  uint8_t y_min_index_ = 0;
  uint8_t y_max_index_ = 0;
  // When calculating overlap, instead of returning the absolute smallest
  // magnitude of overlap on any axis, return the small overlap for any the
  // specified primary axes, but only if there is overlap on all axes.
  absl::flat_hash_map<uint8_t, AxisDirection> primary_axes_;
};

} // namespace zebes
