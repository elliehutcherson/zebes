#include "editor/canvas/canvas.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "objects/camera.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

TEST(CanvasTest, GridSizeIsConfigurable) {
  MockGui mock_gui;
  Canvas::Options options;
  options.gui = &mock_gui;
  options.grid_size = 64.0f;

  Canvas canvas(options);
  EXPECT_EQ(canvas.GetGridSize(), 64.0f);
}

TEST(CanvasTest, DefaultGridSizeIs50) {
  MockGui mock_gui;
  Canvas::Options options;
  options.gui = &mock_gui;

  Canvas canvas(options);
  EXPECT_EQ(canvas.GetGridSize(), 50.0f);
}

TEST(CanvasTest, SetSnapUpdatesSnapValue) {
  MockGui mock_gui;
  Canvas::Options options;
  options.gui = &mock_gui;

  Canvas canvas(options);
  EXPECT_FALSE(canvas.GetSnap());

  canvas.SetSnap(true);
  EXPECT_TRUE(canvas.GetSnap());

  canvas.SetSnap(false);
  EXPECT_FALSE(canvas.GetSnap());
}

TEST(CanvasTest, BeginClampsCameraUsingCurrentViewportOnFirstFrame) {
  NiceMock<MockGui> mock_gui;
  ON_CALL(mock_gui, GetCursorScreenPos()).WillByDefault(Return(ImVec2(0, 0)));

  Canvas canvas({.gui = &mock_gui});
  canvas.SetWorldBounds({0, 0}, {2000, 1000});
  Camera camera;

  canvas.Begin("TestCanvas", ImVec2(800, 600), camera);

  EXPECT_EQ(camera.viewport_width, 744);
  EXPECT_EQ(camera.viewport_height, 580);
  EXPECT_DOUBLE_EQ(camera.position.x, 372);
  EXPECT_DOUBLE_EQ(camera.position.y, 290);

  canvas.End();
}

TEST(CanvasTest, RulerGuttersDoNotOverlapWorldContent) {
  NiceMock<MockGui> mock_gui;
  ON_CALL(mock_gui, GetCursorScreenPos()).WillByDefault(Return(ImVec2(100, 50)));

  Canvas canvas({.gui = &mock_gui});
  canvas.SetWorldBounds({0, 0}, {2000, 1000});
  Camera camera;
  canvas.Begin("TestCanvas", ImVec2(800, 600), camera);

  const ImVec2 screen_center = canvas.WorldToScreen(camera.position);
  EXPECT_FLOAT_EQ(screen_center.x, 528.0f);
  EXPECT_FLOAT_EQ(screen_center.y, 360.0f);
  EXPECT_EQ(canvas.ScreenToWorld(screen_center), camera.position);

  canvas.End();
}

TEST(CanvasTest, HoveredCanvasClaimsMouseWheelWhileZooming) {
  NiceMock<MockGui> mock_gui;
  ImGuiIO io;
  io.MouseWheel = 1.0f;
  ON_CALL(mock_gui, GetCursorScreenPos()).WillByDefault(Return(ImVec2(0, 0)));
  ON_CALL(mock_gui, GetContentRegionAvail()).WillByDefault(Return(ImVec2(800, 600)));
  ON_CALL(mock_gui, IsItemHovered).WillByDefault(Return(true));
  ON_CALL(mock_gui, GetIO()).WillByDefault(ReturnRef(io));

  Canvas canvas({.gui = &mock_gui});
  canvas.SetWorldBounds({0, 0}, {2000, 1000});
  Camera camera;
  canvas.Begin("TestCanvas", ImVec2(800, 600), camera);

  EXPECT_CALL(mock_gui, SetItemKeyOwner(ImGuiKey_MouseWheelY));
  canvas.HandleInput();

  EXPECT_GT(camera.zoom, 1.0);
  canvas.End();
}

}  // namespace
}  // namespace zebes
