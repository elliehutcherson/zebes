#pragma once

#include <cstdint>
#include <optional>

#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace zebes {

enum class SimulationPacingMode {
  kRealtime,
  kUnpaced,
};

struct SimulationPacerConfig {
  absl::Duration step_duration = absl::Seconds(1) / 60;
  int64_t max_steps_per_run = 4;
  absl::Duration max_accumulated_lag = absl::Milliseconds(250);
};

struct SimulationPacingResult {
  int64_t step_count = 0;
  absl::Duration step_duration = absl::ZeroDuration();

  // The fractional progress toward the next simulation step. Whole-step debt
  // remains in accumulated_lag and never makes interpolation extrapolate.
  double interpolation_alpha = 0.0;
  absl::Duration accumulated_lag = absl::ZeroDuration();

  // Elapsed time discarded by this advance because accumulated lag reached its
  // configured bound. A non-zero value is an observable overload, not a normal
  // consequence of max_steps_per_run leaving work for the next pass.
  absl::Duration dropped_lag = absl::ZeroDuration();

  // Time until the next real-time step boundary. It is absent in unpaced mode;
  // zero means whole-step debt remains immediately runnable after this bounded
  // batch. The owning engine translates this monotonic delay into its runner's
  // absolute wake deadline.
  std::optional<absl::Duration> wake_delay;
};

// Converts monotonic elapsed time into bounded batches of fixed simulation
// steps.
//
// Real-time mode retains unprocessed whole-step debt across calls, clamps total
// debt to max_accumulated_lag, and reports exactly how much time was discarded.
// Unpaced mode ignores elapsed time and returns max_steps_per_run on every call.
// Advance never waits, sleeps, or performs I/O. One pacer belongs to one engine
// thread; it is not thread-safe.
class SimulationPacer {
 public:
  static absl::StatusOr<SimulationPacer> Create(SimulationPacerConfig config,
                                                SimulationPacingMode mode);

  absl::StatusOr<SimulationPacingResult> Advance(absl::Duration elapsed);

  const SimulationPacerConfig& config() const { return config_; }
  SimulationPacingMode mode() const { return mode_; }

 private:
  SimulationPacer(SimulationPacerConfig config, SimulationPacingMode mode);

  SimulationPacingResult AdvanceUnpaced() const;
  SimulationPacingResult AdvanceRealtime(absl::Duration elapsed);

  SimulationPacerConfig config_;
  SimulationPacingMode mode_;
  absl::Duration accumulated_lag_ = absl::ZeroDuration();
};

}  // namespace zebes
