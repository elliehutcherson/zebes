#include <memory>

#include "config.h"
#include "controller.h"
#include "vector.h"

namespace zebes {

class Creator {
 public:
  Creator(const GameConfig* config);

  void Update(const ControllerState* state);

 private:
  Point point_;
  const GameConfig* config_;
};

}  // namespace zebes