#include "polygon.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "vector.h"

namespace zebes {
namespace {

Vector Distance(const Point &p1, const Point &p2) { return Vector(p1 - p2); };

} // namespace

const std::vector<Point> *Polygon::vertices() const { return &vertices_; }

float Polygon::x_min() const { return vertices_[x_min_index_].x; }

int Polygon::x_min_floor() const { return vertices_[x_min_index_].x; }

float Polygon::x_max() const { return vertices_[x_max_index_].x; }

int Polygon::x_max_floor() const { return vertices_[x_max_index_].x; }

float Polygon::y_min() const { return vertices_[y_min_index_].y; }

int Polygon::y_min_floor() const { return vertices_[y_min_index_].y; }

float Polygon::y_max() const { return vertices_[y_max_index_].y; }

int Polygon::y_max_floor() const { return vertices_[y_max_index_].y; }

std::vector<Vector> Polygon::GetAxes() const {
  std::vector<Vector> axes;
  for (int i = 0; i < vertices_.size(); ++i) {
    const Point &p1 = vertices_[i];
    const Point &p2 = vertices_[(i + 1) % vertices_.size()];
    Vector axis = Vector(p2 - p1);
    axis = axis.normalize().orthogonal();
    axes.push_back(axis);
  }
  return axes;
}

const absl::flat_hash_map<uint8_t, AxisDirection> *
Polygon::GetPrimaryAxes() const {
  return &primary_axes_;
}

std::optional<AxisOverlap> Polygon::GetOverlapOnAxis(const Polygon &other,
                                                     const Vector &axis) const {
  // Get the minimum and maximum projections of each polygon onto the axis.
  double a_min = 100000;
  double a_max = -100000;
  double b_min = 100000;
  double b_max = -100000;

  for (const Point &p : vertices_) {
    double projection = axis.dot(p);
    a_min = std::min(a_min, projection);
    a_max = std::max(a_max, projection);
  }
  for (const Point &p : *other.vertices()) {
    double projection = axis.dot(p);
    b_min = std::min(b_min, projection);
    b_max = std::max(b_max, projection);
  }

  if (a_max < b_min || b_max < a_min)
    return std::nullopt;

  double a_delta = a_max - b_min;
  double b_delta = b_max - a_min;

  return AxisOverlap{
      .axis = axis,
      .left_distance = a_delta,
      .right_distance = -b_delta,
  };
}

PolygonOverlap Polygon::GetOverlap(const Polygon &other) const {
  PolygonOverlap polygon_overlap;
  int axis_index = 0;
  // Check each axis to see if it separates the two polygons.
  for (const Vector &axis : GetAxes()) {
    std::optional<AxisOverlap> axis_overlap = GetOverlapOnAxis(other, axis);
    if (!axis_overlap.has_value())
      return polygon_overlap;
    const auto primary_iter = primary_axes_.find(axis_index++);
    if (primary_iter != primary_axes_.end()) {
      axis_overlap->is_primary = true;
      axis_overlap->primary_axis_direction = primary_iter->second;
    }
    polygon_overlap.AddAxisOverlap(*axis_overlap);
  }

  axis_index = 0;
  for (const Vector &axis : other.GetAxes()) {
    std::optional<AxisOverlap> axis_overlap = GetOverlapOnAxis(other, axis);
    if (!axis_overlap.has_value())
      return polygon_overlap;
    const auto primary_iter = other.GetPrimaryAxes()->find(axis_index++);
    if (primary_iter != primary_axes_.end()) {
      axis_overlap->is_primary = true;
      axis_overlap->primary_axis_direction = primary_iter->second;
    }
    polygon_overlap.AddAxisOverlap(*axis_overlap);
  }

  // If we reach this point, then the two polygons do overlap.
  polygon_overlap.overlap = true;
  return polygon_overlap;
}

absl::Status Polygon::AddPrimaryAxisIndex(uint8_t index,
                                          AxisDirection axis_direction) {
  if (index >= vertices_.size())
    return absl::OutOfRangeError(
        "Index out of range when tryting to add primary axis.");
  primary_axes_[index] = axis_direction;
  return absl::OkStatus();
}

void Polygon::Move(float x, float y) {
  for (Point &vertex : vertices_) {
    vertex.x += x;
    vertex.y += y;
  }
}

} // namespace zebes