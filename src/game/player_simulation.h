#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "engine/input_manager_interface.h"
#include "game/game_engine.h"
#include "game/runtime_world.h"
#include "objects/camera.h"

namespace zebes {

// Fixed-tick gameplay simulation for M2. It owns RuntimeWorld, borrows the
// render-frame input manager, and follows the committed player transform with
// a platform-neutral camera. It performs no I/O or native rendering work.
class PlayerSimulation final : public GameSimulation {
 public:
  struct Options {
    IInputManager* input_manager = nullptr;
    std::unique_ptr<RuntimeWorld> world;
    Camera camera;
    PlayerMovementConfig movement;
  };

  static absl::StatusOr<std::unique_ptr<PlayerSimulation>> Create(Options options);

  absl::Status Step(absl::Duration duration) override;

  const Camera& camera() const { return camera_; }
  const RuntimeWorld& world() const { return *world_; }
  RuntimeWorld& world() { return *world_; }

 private:
  explicit PlayerSimulation(Options options);

  IInputManager& input_manager_;
  std::unique_ptr<RuntimeWorld> world_;
  Camera camera_;
  PlayerMovementConfig movement_;
};

}  // namespace zebes
