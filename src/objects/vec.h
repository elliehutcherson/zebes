#pragma once

namespace zebes {

struct Vec {
  double x = 0;
  double y = 0;

  // Equality: Checks exact match
  bool operator==(const Vec& other) const { return x == other.x && y == other.y; }

  // Inequality: Inverse of equality
  bool operator!=(const Vec& other) const { return !(*this == other); }

  // Less Than: Sorts by X, then by Y (Lexicographical)
  // Required for std::map keys or std::sort
  bool operator<(const Vec& other) const {
    if (x != other.x) {
      return x < other.x;
    }
    return y < other.y;
  }
};

}  // namespace zebes