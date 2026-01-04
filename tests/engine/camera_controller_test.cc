#include "engine/camera_controller.h"

#include <memory>

#include "common/mock_input_manager.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/camera.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class CameraControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    camera_ = std::make_unique<Camera>();
    // Default camera setups
    camera_->position = {0, 0};
    camera_->zoom = 1.0;

    mock_input_ = std::make_unique<NiceMock<MockInputManager>>();

    CameraController::Options options;
    options.camera = camera_.get();
    options.input_manager =
        mock_input_.get();  // Checks out because MockInputManager -> IInputManager
    options.move_speed = 100.0;
    options.zoom_speed = 1.0;

    auto controller_or = CameraController::Create(options);
    ASSERT_TRUE(controller_or.ok());
    controller_ = std::move(controller_or.value());

    // Default behavior: Return false for any action not explicitly expected
    EXPECT_CALL(*mock_input_, IsActionActive(_)).WillRepeatedly(Return(false));
  }

  std::unique_ptr<Camera> camera_;
  std::unique_ptr<NiceMock<MockInputManager>> mock_input_;
  std::unique_ptr<CameraController> controller_;
};

TEST_F(CameraControllerTest, PanningUpMovesCameraYNegative) {
  // Logic: PanUp -> movement.y -= 1
  // new_pos = old_pos + (0, -1) * speed * dt

  EXPECT_CALL(*mock_input_, IsActionActive("PanUp")).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_input_, IsActionActive("PanDown")).WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_input_, IsActionActive("PanLeft")).WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_input_, IsActionActive("PanRight")).WillRepeatedly(Return(false));

  controller_->Update(1.0);  // 1 second delta

  // Speed is 100, so expected Y is -100
  EXPECT_DOUBLE_EQ(camera_->position.y, -100.0);
  EXPECT_DOUBLE_EQ(camera_->position.x, 0.0);
}

TEST_F(CameraControllerTest, PanningDownMovesCameraYPositive) {
  EXPECT_CALL(*mock_input_, IsActionActive("PanUp")).WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_input_, IsActionActive("PanDown")).WillRepeatedly(Return(true));

  controller_->Update(0.5);  // 0.5 second delta

  // Speed 100 * 0.5 = 50
  EXPECT_DOUBLE_EQ(camera_->position.y, 50.0);
}

TEST_F(CameraControllerTest, PanningLeftMovesCameraXNegative) {
  EXPECT_CALL(*mock_input_, IsActionActive("PanLeft")).WillRepeatedly(Return(true));

  controller_->Update(1.0);

  EXPECT_DOUBLE_EQ(camera_->position.x, -100.0);
}

TEST_F(CameraControllerTest, PanningRightMovesCameraXPositive) {
  EXPECT_CALL(*mock_input_, IsActionActive("PanRight")).WillRepeatedly(Return(true));

  controller_->Update(1.0);

  EXPECT_DOUBLE_EQ(camera_->position.x, 100.0);
}

TEST_F(CameraControllerTest, ZoomInIncreasesZoom) {
  EXPECT_CALL(*mock_input_, IsActionActive("ZoomIn")).WillRepeatedly(Return(true));

  // Initial zoom 1.0, speed 1.0
  controller_->Update(0.5);

  EXPECT_DOUBLE_EQ(camera_->zoom, 1.5);
}

TEST_F(CameraControllerTest, ZoomOutDecreasesZoom) {
  EXPECT_CALL(*mock_input_, IsActionActive("ZoomOut")).WillRepeatedly(Return(true));

  // Initial zoom 1.0, speed 1.0
  controller_->Update(0.2);

  EXPECT_DOUBLE_EQ(camera_->zoom, 0.8);
}

TEST_F(CameraControllerTest, ZoomClampedToMin) {
  EXPECT_CALL(*mock_input_, IsActionActive("ZoomOut")).WillRepeatedly(Return(true));

  // Try to zoom out way past 0.1
  // Start 1.0, subctract 10.0 * 1.0 -> -9.0
  // Should clamp to 0.1
  controller_->Update(10.0);

  EXPECT_DOUBLE_EQ(camera_->zoom, 0.1);
}

TEST_F(CameraControllerTest, ZoomClampedToMax) {
  EXPECT_CALL(*mock_input_, IsActionActive("ZoomIn")).WillRepeatedly(Return(true));

  // Try to zoom in way past 5.0
  controller_->Update(10.0);

  EXPECT_DOUBLE_EQ(camera_->zoom, 5.0);
}

TEST_F(CameraControllerTest, MoveSpeedAffectedByZoom) {
  // If we are zoomed in (zoom=2.0), we cover less world space per screen pixel.
  // The code divides speed by zoom: speed = move_speed_ / camera_.zoom;
  // So if zoom is 2.0, effective speed is 50.

  camera_->zoom = 2.0;
  EXPECT_CALL(*mock_input_, IsActionActive("PanRight")).WillRepeatedly(Return(true));

  controller_->Update(1.0);

  EXPECT_DOUBLE_EQ(camera_->position.x, 50.0);
}

TEST_F(CameraControllerTest, CreateReturnsErrorOnNulls) {
  CameraController::Options options;
  options.camera = nullptr;
  options.input_manager = mock_input_.get();

  auto result = CameraController::Create(options);
  EXPECT_FALSE(result.ok());

  options.camera = camera_.get();
  options.input_manager = nullptr;
  result = CameraController::Create(options);
  EXPECT_FALSE(result.ok());
}

}  // namespace
}  // namespace zebes
