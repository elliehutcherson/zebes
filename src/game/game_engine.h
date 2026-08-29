#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "common/engine.h"
#include "common/notification_set.h"
#include "game/simulation_pacer.h"

namespace zebes {

struct GameTimingState {
  uint64_t completed_step_count = 0;
  absl::Duration simulation_time = absl::ZeroDuration();
  double interpolation_alpha = 0.0;
  size_t overrun_count = 0;
  absl::Duration total_dropped_lag = absl::ZeroDuration();
};

// Owns the runtime world and advances it by one fixed simulation duration.
// Step must be bounded and non-blocking; it must not wait or perform I/O.
class GameSimulation {
 public:
  virtual ~GameSimulation() = default;
  virtual absl::Status Step(absl::Duration duration) = 0;
};

// The platform-neutral simulation engine boundary. It owns its simulation and
// advances it only in fixed-duration steps selected by SimulationPacer.
//
// Milestone 1 may drive Run inline from the render loop. The notification set
// exists now so the same engine can move unchanged onto EngineRunner later.
class GameEngine final : public Engine {
 public:
  static absl::StatusOr<std::unique_ptr<GameEngine>> Create(
      SimulationPacerConfig config, SimulationPacingMode mode,
      std::unique_ptr<GameSimulation> simulation);

  NotificationSet& notification_set() override { return *notification_set_; }
  absl::StatusOr<RunResult> Run() override;

  const GameTimingState& timing_state() const { return timing_state_; }

 private:
  GameEngine(SimulationPacer pacer, std::unique_ptr<GameSimulation> simulation,
             std::unique_ptr<NotificationSet> notification_set);

  absl::Duration MeasureElapsedTime();
  absl::Status AdvanceSimulation(const SimulationPacingResult& pacing);
  static RunResult BuildRunResult(const SimulationPacingResult& pacing);

  SimulationPacer pacer_;
  std::unique_ptr<GameSimulation> simulation_;
  std::unique_ptr<NotificationSet> notification_set_;
  std::optional<std::chrono::steady_clock::time_point> previous_run_;
  GameTimingState timing_state_;
};

}  // namespace zebes
