#pragma once

#include <memory>

#include "engine/controller.h"
#include "engine/focus.h"

namespace zebes {

class FocusLite : public Focus {
public:
  ~FocusLite() = default;

  void Update(const ControllerState *state) override;

  float x_center() const override;

  float y_center() const override;

  std::string to_string() const override;

 private:
  Point world_position_;
};

}  // namespace zebes