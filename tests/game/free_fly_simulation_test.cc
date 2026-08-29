#include "game/free_fly_simulation.h"

#include <memory>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "common/mock_input_manager.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/camera.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

Camera TestCamera() {
  return Camera{
      .position = {100.0, 200.0},
      .zoom = 1.0,
      .viewport_width = 960,
      .viewport_height = 540,
  };
}

TEST(FreeFlySimulationTest, AdvancesCameraFromHeldInputByFixedStepDuration) {
  NiceMock<MockInputManager> input;
  EXPECT_CALL(input, IsActionActive(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(input, IsActionActive("PanRight")).WillRepeatedly(Return(true));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<FreeFlySimulation> simulation, FreeFlySimulation::Create({
                                                                          .input_manager = &input,
                                                                          .camera = TestCamera(),
                                                                          .move_speed = 480.0,
                                                                      }));

  ASSERT_OK(simulation->Step(absl::Milliseconds(50)));

  EXPECT_DOUBLE_EQ(simulation->camera().position.x, 124.0);
  EXPECT_DOUBLE_EQ(simulation->camera().position.y, 200.0);
}

TEST(FreeFlySimulationTest, ZoomClampsToRuntimeRange) {
  NiceMock<MockInputManager> input;
  EXPECT_CALL(input, IsActionActive(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(input, IsActionActive("ZoomOut")).WillRepeatedly(Return(true));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<FreeFlySimulation> simulation,
                       FreeFlySimulation::Create({
                           .input_manager = &input,
                           .camera = TestCamera(),
                           .zoom_speed = 10.0,
                           .zoom_range = {.minimum = 0.5, .maximum = 2.0},
                       }));

  ASSERT_OK(simulation->Step(absl::Seconds(1)));

  EXPECT_DOUBLE_EQ(simulation->camera().zoom, 0.5);
}

TEST(FreeFlySimulationTest, RejectsInvalidOptionsAndStepDuration) {
  NiceMock<MockInputManager> input;
  EXPECT_FALSE(FreeFlySimulation::Create({.camera = TestCamera()}).ok());
  EXPECT_FALSE(FreeFlySimulation::Create({
                                             .input_manager = &input,
                                             .camera = TestCamera(),
                                             .move_speed = -1.0,
                                         })
                   .ok());
  Camera invalid_camera = TestCamera();
  invalid_camera.viewport_width = 0;
  EXPECT_FALSE(FreeFlySimulation::Create({.input_manager = &input, .camera = invalid_camera}).ok());

  EXPECT_CALL(input, IsActionActive(_)).WillRepeatedly(Return(false));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FreeFlySimulation> simulation,
      FreeFlySimulation::Create({.input_manager = &input, .camera = TestCamera()}));
  EXPECT_EQ(simulation->Step(absl::ZeroDuration()).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(simulation->Step(absl::InfiniteDuration()).code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
