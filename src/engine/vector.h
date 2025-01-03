#pragma once

#include <cmath>

#include "absl/strings/str_format.h"
#include "cereal/archives/binary.hpp"

namespace zebes {

struct Point {
  double x = 0;
  double y = 0;

  int x_floor() const { return static_cast<int>(x); }
  int y_floor() const { return static_cast<int>(y); }

  std::string to_string() const {
    return absl::StrFormat("{x: %f, y: %f}", x, y);
  };
  Point operator-(const Point &p) const {
    return {.x = this->x - p.x, .y = this->y - p.y};
  }
  Point operator+(const Point &p) const {
    return {.x = this->x + p.x, .y = this->y + p.y};
  }
  bool operator==(const Point &p) const {
    return this->x == p.x && this->y == p.y;
  }
  bool operator<(const Point &p) const {
    return this->x < p.x || (this->x == p.x && this->y < p.y);
  }

  template <class Archive>
  void serialize(Archive &archive) {
    archive(x, y);
  }

  template <class Archive>
  void load(Archive &archive) {
    archive(x, y);
  }
};

struct PointHash {
  std::size_t operator()(const Point &p) const {
    std::size_t h1 = std::hash<double>()(p.x);
    std::size_t h2 = std::hash<double>()(p.y);
    return h1 ^ (h2 << 1);  // Combine the two hashes
  }
};

struct Vector {
  double x = 0;
  double y = 0;

  Vector() = default;

  // Convert from point to vector.
  explicit Vector(const Point &p) {
    x = p.x;
    y = p.y;
  };

  // Normal vector
  Vector normalize() {
    double length = std::sqrt(x * x + y * y);
    Vector v;
    v.x = x / length;
    v.y = y / length;
    return v;
  }

  // Orthoganl vector
  Vector orthogonal() {
    Vector v;
    v.x = -y;
    v.y = x;
    return v;
  }

  // Dot product with a vector
  double dot(const Vector &v) const { return x * v.x + y * v.y; }

  // Dot product with a point.
  double dot(const Point &p) const { return x * p.x + y * p.y; }

  // Simple string to print x and y.
  std::string ToString() const { return absl::StrFormat("x: %f, y: %f", x, y); }

  template <class Archive>
  void serialize(Archive &archive) {
    archive(x, y);
  }

  template <class Archive>
  void load(Archive &archive) {
    archive(x, y);
  }
};

}  // namespace zebes