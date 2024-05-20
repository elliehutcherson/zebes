#pragma once

#include <memory>

#include "controller.h"

namespace zebes {

class Focus {
public:
  ~Focus() = default;

  virtual void Update(const ControllerState *state) = 0;

  virtual float x_center() const = 0;

  virtual float y_center() const = 0;

  virtual std::string to_string() const = 0;
};

} // namespace zebes