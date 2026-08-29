#include "game/simulation_pacer.h"

#include <cstddef>
#include <limits>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "common/status_macros.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

constexpr absl::Duration kStep = absl::Milliseconds(10);

SimulationPacerConfig TestConfig() {
  return SimulationPacerConfig{
      .step_duration = kStep,
      .max_steps_per_run = 8,
      .max_accumulated_lag = absl::Milliseconds(100),
  };
}

absl::StatusOr<size_t> CountSteps(absl::Duration render_cadence, size_t frame_count) {
  ASSIGN_OR_RETURN(SimulationPacer pacer,
                   SimulationPacer::Create(TestConfig(), SimulationPacingMode::kRealtime));
  size_t step_count = 0;
  for (size_t frame = 0; frame < frame_count; ++frame) {
    ASSIGN_OR_RETURN(const SimulationPacingResult result, pacer.Advance(render_cadence));
    step_count += result.step_count;
  }
  return step_count;
}

TEST(SimulationPacerTest, RealtimeCadenceReturnsDueStepsDeadlineAndInterpolation) {
  ASSERT_OK_AND_ASSIGN(SimulationPacer pacer,
                       SimulationPacer::Create(TestConfig(), SimulationPacingMode::kRealtime));
  ASSERT_OK_AND_ASSIGN(const SimulationPacingResult initial, pacer.Advance(absl::ZeroDuration()));
  EXPECT_EQ(initial.step_count, 0);
  EXPECT_EQ(initial.interpolation_alpha, 0.0);
  EXPECT_EQ(initial.wake_delay, kStep);

  ASSERT_OK_AND_ASSIGN(const SimulationPacingResult partial, pacer.Advance(absl::Milliseconds(4)));
  EXPECT_EQ(partial.step_count, 0);
  EXPECT_DOUBLE_EQ(partial.interpolation_alpha, 0.4);
  EXPECT_EQ(partial.accumulated_lag, absl::Milliseconds(4));
  EXPECT_EQ(partial.wake_delay, absl::Milliseconds(6));

  ASSERT_OK_AND_ASSIGN(const SimulationPacingResult due, pacer.Advance(absl::Milliseconds(6)));
  EXPECT_EQ(due.step_count, 1);
  EXPECT_EQ(due.interpolation_alpha, 0.0);
  EXPECT_EQ(due.accumulated_lag, absl::ZeroDuration());
  EXPECT_EQ(due.wake_delay, kStep);

  ASSERT_OK_AND_ASSIGN(const SimulationPacingResult between, pacer.Advance(absl::Milliseconds(15)));
  EXPECT_EQ(between.step_count, 1);
  EXPECT_DOUBLE_EQ(between.interpolation_alpha, 0.5);
  EXPECT_EQ(between.wake_delay, absl::Milliseconds(5));
}

TEST(SimulationPacerTest, SimulationRateIsIndependentOfRenderCadence) {
  ASSERT_OK_AND_ASSIGN(const size_t fast_render_steps, CountSteps(absl::Milliseconds(5), 20));
  ASSERT_OK_AND_ASSIGN(const size_t slow_render_steps, CountSteps(absl::Milliseconds(20), 5));

  EXPECT_EQ(fast_render_steps, 10);
  EXPECT_EQ(slow_render_steps, 10);
}

TEST(SimulationPacerTest, OverloadClampsLagAndRetainsOnlyBoundedStepDebt) {
  SimulationPacerConfig config{
      .step_duration = kStep,
      .max_steps_per_run = 3,
      .max_accumulated_lag = absl::Milliseconds(55),
  };
  ASSERT_OK_AND_ASSIGN(SimulationPacer pacer,
                       SimulationPacer::Create(config, SimulationPacingMode::kRealtime));
  ASSERT_OK_AND_ASSIGN(const SimulationPacingResult overloaded,
                       pacer.Advance(absl::Milliseconds(100)));
  EXPECT_EQ(overloaded.step_count, 3);
  EXPECT_EQ(overloaded.dropped_lag, absl::Milliseconds(45));
  EXPECT_EQ(overloaded.accumulated_lag, absl::Milliseconds(25));
  EXPECT_DOUBLE_EQ(overloaded.interpolation_alpha, 0.5);
  EXPECT_EQ(overloaded.wake_delay, absl::ZeroDuration());

  ASSERT_OK_AND_ASSIGN(const SimulationPacingResult catch_up, pacer.Advance(absl::ZeroDuration()));
  EXPECT_EQ(catch_up.step_count, 2);
  EXPECT_EQ(catch_up.dropped_lag, absl::ZeroDuration());
  EXPECT_EQ(catch_up.accumulated_lag, absl::Milliseconds(5));
  EXPECT_DOUBLE_EQ(catch_up.interpolation_alpha, 0.5);
  EXPECT_EQ(catch_up.wake_delay, absl::Milliseconds(5));
}

TEST(SimulationPacerTest, UnpacedModeReturnsABoundedBatchWithoutConsultingWallTime) {
  SimulationPacerConfig config{
      .step_duration = kStep,
      .max_steps_per_run = 3,
      .max_accumulated_lag = absl::Milliseconds(50),
  };
  ASSERT_OK_AND_ASSIGN(SimulationPacer pacer,
                       SimulationPacer::Create(config, SimulationPacingMode::kUnpaced));

  ASSERT_OK_AND_ASSIGN(const SimulationPacingResult first, pacer.Advance(absl::InfiniteDuration()));
  ASSERT_OK_AND_ASSIGN(const SimulationPacingResult second,
                       pacer.Advance(-absl::InfiniteDuration()));
  EXPECT_EQ(first.step_count, 3);
  EXPECT_EQ(second.step_count, 3);
  EXPECT_EQ(first.step_duration, kStep);
  EXPECT_EQ(first.interpolation_alpha, 0.0);
  EXPECT_EQ(first.dropped_lag, absl::ZeroDuration());
  EXPECT_FALSE(first.wake_delay.has_value());
}

TEST(SimulationPacerTest, RejectsInvalidConfiguration) {
  SimulationPacerConfig config = TestConfig();
  config.step_duration = absl::ZeroDuration();
  EXPECT_EQ(SimulationPacer::Create(config, SimulationPacingMode::kRealtime).status().code(),
            absl::StatusCode::kInvalidArgument);

  config = TestConfig();
  config.step_duration = absl::InfiniteDuration();
  EXPECT_EQ(SimulationPacer::Create(config, SimulationPacingMode::kRealtime).status().code(),
            absl::StatusCode::kInvalidArgument);

  config = TestConfig();
  config.max_steps_per_run = 0;
  EXPECT_EQ(SimulationPacer::Create(config, SimulationPacingMode::kRealtime).status().code(),
            absl::StatusCode::kInvalidArgument);

  config = TestConfig();
  config.max_steps_per_run = std::numeric_limits<size_t>::max();
  EXPECT_EQ(SimulationPacer::Create(config, SimulationPacingMode::kRealtime).status().code(),
            absl::StatusCode::kInvalidArgument);

  config = TestConfig();
  config.max_accumulated_lag = absl::Milliseconds(9);
  EXPECT_EQ(SimulationPacer::Create(config, SimulationPacingMode::kRealtime).status().code(),
            absl::StatusCode::kInvalidArgument);

  config = TestConfig();
  config.max_accumulated_lag = absl::InfiniteDuration();
  EXPECT_EQ(SimulationPacer::Create(config, SimulationPacingMode::kRealtime).status().code(),
            absl::StatusCode::kInvalidArgument);

  EXPECT_EQ(
      SimulationPacer::Create(TestConfig(), static_cast<SimulationPacingMode>(-1)).status().code(),
      absl::StatusCode::kInvalidArgument);
}

TEST(SimulationPacerTest, RejectsInvalidRealtimeElapsedTime) {
  ASSERT_OK_AND_ASSIGN(SimulationPacer pacer,
                       SimulationPacer::Create(TestConfig(), SimulationPacingMode::kRealtime));

  EXPECT_EQ(pacer.Advance(absl::InfiniteDuration()).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(pacer.Advance(-absl::Nanoseconds(1)).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
