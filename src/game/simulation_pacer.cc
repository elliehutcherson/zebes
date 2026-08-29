#include "game/simulation_pacer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace zebes {
namespace {

absl::Status ValidateConfig(const SimulationPacerConfig& config, SimulationPacingMode mode) {
  if (config.step_duration <= absl::ZeroDuration() ||
      config.step_duration == absl::InfiniteDuration()) {
    return absl::InvalidArgumentError("Simulation step duration must be finite and positive");
  }
  if (config.max_steps_per_run == 0 ||
      config.max_steps_per_run > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
    return absl::InvalidArgumentError("Simulation max steps per run must fit in a positive int64");
  }
  if (config.max_accumulated_lag < config.step_duration ||
      config.max_accumulated_lag == absl::InfiniteDuration()) {
    return absl::InvalidArgumentError(
        "Simulation maximum accumulated lag must be finite and at least one step");
  }
  if (mode != SimulationPacingMode::kRealtime && mode != SimulationPacingMode::kUnpaced) {
    return absl::InvalidArgumentError("Unknown simulation pacing mode");
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<SimulationPacer> SimulationPacer::Create(SimulationPacerConfig config,
                                                        SimulationPacingMode mode) {
  const absl::Status status = ValidateConfig(config, mode);
  if (!status.ok()) return status;
  return SimulationPacer(std::move(config), mode);
}

SimulationPacer::SimulationPacer(SimulationPacerConfig config, SimulationPacingMode mode)
    : config_(std::move(config)), mode_(mode) {}

absl::StatusOr<SimulationPacingResult> SimulationPacer::Advance(absl::Duration elapsed) {
  if (mode_ == SimulationPacingMode::kUnpaced) return AdvanceUnpaced();
  if (elapsed < absl::ZeroDuration() || elapsed == absl::InfiniteDuration()) {
    return absl::InvalidArgumentError(
        "Simulation pacing elapsed time must be finite and non-negative");
  }
  return AdvanceRealtime(elapsed);
}

SimulationPacingResult SimulationPacer::AdvanceUnpaced() const {
  return SimulationPacingResult{
      .step_count = config_.max_steps_per_run,
      .step_duration = config_.step_duration,
  };
}

SimulationPacingResult SimulationPacer::AdvanceRealtime(absl::Duration elapsed) {
  accumulated_lag_ += elapsed;

  absl::Duration dropped_lag = absl::ZeroDuration();
  if (accumulated_lag_ > config_.max_accumulated_lag) {
    dropped_lag = accumulated_lag_ - config_.max_accumulated_lag;
    accumulated_lag_ = config_.max_accumulated_lag;
  }

  absl::Duration fractional_lag;
  const int64_t available_steps =
      absl::IDivDuration(accumulated_lag_, config_.step_duration, &fractional_lag);
  const int64_t step_count =
      std::min(available_steps, static_cast<int64_t>(config_.max_steps_per_run));
  accumulated_lag_ -= config_.step_duration * step_count;

  const double interpolation_alpha = absl::FDivDuration(fractional_lag, config_.step_duration);
  const std::optional<absl::Duration> wake_delay =
      accumulated_lag_ >= config_.step_duration
          ? std::optional<absl::Duration>(absl::ZeroDuration())
          : std::optional<absl::Duration>(config_.step_duration - accumulated_lag_);

  return SimulationPacingResult{
      .step_count = static_cast<size_t>(step_count),
      .step_duration = config_.step_duration,
      .interpolation_alpha = interpolation_alpha,
      .accumulated_lag = accumulated_lag_,
      .dropped_lag = dropped_lag,
      .wake_delay = wake_delay,
  };
}

}  // namespace zebes
