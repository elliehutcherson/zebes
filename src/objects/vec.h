#pragma once

#include <cmath>

#include "absl/strings/str_format.h"

namespace zebes {

struct Vec {
  double x = 0;
  double y = 0;

  bool operator==(const Vec&) const = default;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Vec& v) {
    absl::Format(&sink, "(%g, %g)", v.x, v.y);
  }
};

constexpr Vec operator+(Vec left, Vec right) {
  return {.x = left.x + right.x, .y = left.y + right.y};
}

constexpr Vec operator-(Vec left, Vec right) {
  return {.x = left.x - right.x, .y = left.y - right.y};
}

constexpr Vec operator*(Vec value, double scale) {
  return {.x = value.x * scale, .y = value.y * scale};
}

constexpr Vec operator*(double scale, Vec value) { return value * scale; }

constexpr double Dot(Vec left, Vec right) { return left.x * right.x + left.y * right.y; }

inline bool IsFinite(Vec value) { return std::isfinite(value.x) && std::isfinite(value.y); }

}  // namespace zebes
