#pragma once

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

}  // namespace zebes
