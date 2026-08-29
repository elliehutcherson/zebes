#include "game/game_engine.h"

#include <chrono>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "common/engine.h"
#include "common/notification_set.h"
#include "common/status_macros.h"
#include "game/simulation_pacer.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<GameEngine>> GameEngine::Create(
    SimulationPacerConfig config, SimulationPacingMode mode,
    std::unique_ptr<GameSimulation> simulation) {
  if (simulation == nullptr) {
    return absl::InvalidArgumentError("Game engine requires a simulation");
  }
  ASSIGN_OR_RETURN(SimulationPacer pacer, SimulationPacer::Create(std::move(config), mode));
  ASSIGN_OR_RETURN(std::unique_ptr<NotificationSet> notification_set, NotificationSet::Create());
  return std::unique_ptr<GameEngine>(
      new GameEngine(std::move(pacer), std::move(simulation), std::move(notification_set)));
}

GameEngine::GameEngine(SimulationPacer pacer, std::unique_ptr<GameSimulation> simulation,
                       std::unique_ptr<NotificationSet> notification_set)
    : pacer_(std::move(pacer)),
      simulation_(std::move(simulation)),
      notification_set_(std::move(notification_set)) {}

absl::StatusOr<RunResult> GameEngine::Run() {
  ASSIGN_OR_RETURN(const SimulationPacingResult pacing, pacer_.Advance(MeasureElapsedTime()));
  RETURN_IF_ERROR(AdvanceSimulation(pacing));
  return BuildRunResult(pacing);
}

absl::Duration GameEngine::MeasureElapsedTime() {
  const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  absl::Duration elapsed = absl::ZeroDuration();
  if (previous_run_.has_value()) {
    const int64_t elapsed_nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - *previous_run_).count();
    elapsed = absl::Nanoseconds(elapsed_nanoseconds);
  }
  previous_run_ = now;
  return elapsed;
}

absl::Status GameEngine::AdvanceSimulation(const SimulationPacingResult& pacing) {
  for (int64_t step = 0; step < pacing.step_count; ++step) {
    RETURN_IF_ERROR(simulation_->Step(pacing.step_duration));
    ++timing_state_.completed_step_count;
    timing_state_.simulation_time += pacing.step_duration;
  }
  timing_state_.interpolation_alpha = pacing.interpolation_alpha;
  if (pacing.dropped_lag > absl::ZeroDuration()) {
    ++timing_state_.overrun_count;
    timing_state_.total_dropped_lag += pacing.dropped_lag;
  }
  return absl::OkStatus();
}

RunResult GameEngine::BuildRunResult(const SimulationPacingResult& pacing) {
  if (pacing.step_count > 0) {
    return RunResult{.feedback = RunFeedback::kDidWork};
  }
  const std::optional<absl::Time> wake_deadline =
      pacing.wake_delay.has_value() ? std::optional<absl::Time>(absl::Now() + *pacing.wake_delay)
                                    : std::nullopt;
  return {
      .feedback = RunFeedback::kIdle,
      .wake_deadline = wake_deadline,
  };
}

}  // namespace zebes
