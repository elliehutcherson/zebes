#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace zebes {

class DbInterface {
 public:
  virtual ~DbInterface() = default;
};

}  // namespace zebes
