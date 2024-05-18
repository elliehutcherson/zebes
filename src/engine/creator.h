#pragma once

#include <memory>
#include <string>

#include "config.h"
#include "controller.h"
#include "vector.h"
#include "focus.h"

namespace zebes {

class Creator : public Focus {
 public:
  Creator(const GameConfig* config);

  void Update(const ControllerState* state) override;

  float x_center() const override;
  
  float y_center() const override;

  std::string to_string() const override; 

 private:
  Point point_;
  const GameConfig* config_;
};

}  // namespace zebes