#include "game/game_engine.h"

#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "common/engine.h"
#include "game/simulation_pacer.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

class RecordingSimulation final : public GameSimulation {
 public:
  absl::Status Step(absl::Duration duration) override {
    step_durations_.push_back(duration);
    if (fail_on_step_ == step_durations_.size()) {
      return absl::DataLossError("simulation step failed");
    }
    return absl::OkStatus();
  }

  void set_fail_on_step(size_t step) { fail_on_step_ = step; }
  const std::vector<absl::Duration>& step_durations() const { return step_durations_; }

 private:
  size_t fail_on_step_ = 0;
  std::vector<absl::Duration> step_durations_;
};

SimulationPacerConfig TestConfig() {
  return SimulationPacerConfig{
      .step_duration = absl::Milliseconds(10),
      .max_steps_per_run = 3,
      .max_accumulated_lag = absl::Milliseconds(50),
  };
}

TEST(GameEngineTest, UnpacedRunsAdvanceBoundedFixedStepBatches) {
  auto simulation = std::make_unique<RecordingSimulation>();
  RecordingSimulation* simulation_pointer = simulation.get();
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<GameEngine> engine,
      GameEngine::Create(TestConfig(), SimulationPacingMode::kUnpaced, std::move(simulation)));

  ASSERT_OK_AND_ASSIGN(const RunResult first, engine->Run());
  EXPECT_EQ(first.feedback, RunFeedback::kDidWork);
  EXPECT_FALSE(first.wake_deadline.has_value());
  EXPECT_EQ(engine->timing_state().completed_step_count, 3);
  EXPECT_EQ(engine->timing_state().simulation_time, absl::Milliseconds(30));

  ASSERT_OK_AND_ASSIGN(const RunResult second, engine->Run());
  EXPECT_EQ(second.feedback, RunFeedback::kDidWork);
  EXPECT_EQ(engine->timing_state().completed_step_count, 6);
  EXPECT_EQ(engine->timing_state().simulation_time, absl::Milliseconds(60));
  EXPECT_EQ(engine->timing_state().interpolation_alpha, 0.0);
  EXPECT_EQ(engine->timing_state().overrun_count, 0);
  EXPECT_EQ(engine->timing_state().total_dropped_lag, absl::ZeroDuration());
  ASSERT_EQ(simulation_pointer->step_durations().size(), 6);
  for (absl::Duration duration : simulation_pointer->step_durations()) {
    EXPECT_EQ(duration, absl::Milliseconds(10));
  }
}

TEST(GameEngineTest, FirstRealtimeRunIsIdleUntilItsFixedStepDeadline) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<GameEngine> engine,
                       GameEngine::Create(TestConfig(), SimulationPacingMode::kRealtime,
                                          std::make_unique<RecordingSimulation>()));

  ASSERT_OK_AND_ASSIGN(const RunResult result, engine->Run());
  EXPECT_EQ(result.feedback, RunFeedback::kIdle);
  EXPECT_TRUE(result.wake_deadline.has_value());
  EXPECT_EQ(engine->timing_state().completed_step_count, 0);
  EXPECT_EQ(engine->notification_set().size(), 0);
}

TEST(GameEngineTest, PropagatesSimulationFailureAfterRecordingCompletedSteps) {
  auto simulation = std::make_unique<RecordingSimulation>();
  simulation->set_fail_on_step(2);
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<GameEngine> engine,
      GameEngine::Create(TestConfig(), SimulationPacingMode::kUnpaced, std::move(simulation)));

  const absl::Status status = engine->Run().status();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(status.message(), "simulation step failed");
  EXPECT_EQ(engine->timing_state().completed_step_count, 1);
  EXPECT_EQ(engine->timing_state().simulation_time, absl::Milliseconds(10));
}

TEST(GameEngineTest, RejectsInvalidPacingConfigurationBeforeConstruction) {
  SimulationPacerConfig config = TestConfig();
  config.max_steps_per_run = 0;

  EXPECT_FALSE(GameEngine::Create(config, SimulationPacingMode::kRealtime,
                                  std::make_unique<RecordingSimulation>())
                   .ok());
}

TEST(GameEngineTest, RejectsMissingSimulation) {
  EXPECT_FALSE(GameEngine::Create(TestConfig(), SimulationPacingMode::kRealtime, nullptr).ok());
}

}  // namespace
}  // namespace zebes
