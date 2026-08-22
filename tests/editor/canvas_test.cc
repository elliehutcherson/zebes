#include "editor/canvas/canvas.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "objects/camera.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

// A canvas wired to a mock, with the cursor position under the test's control.
// Every viewport in the editor is one of these, so panning behaviour proved
// here is the behaviour all of them get.
struct PanHarness {
  PanHarness() {
    io.MouseWheel = 0.0f;
    io.DeltaTime = 0.0f;
    ON_CALL(gui, GetCursorScreenPos()).WillByDefault(Return(ImVec2(0, 0)));
    ON_CALL(gui, GetIO()).WillByDefault(ReturnRef(io));
    ON_CALL(gui, GetMousePos()).WillByDefault(Invoke([this] { return cursor; }));
    ON_CALL(gui, IsItemHovered(_)).WillByDefault(Invoke([this](ImGuiHoveredFlags) {
      return hovered;
    }));
    ON_CALL(gui, IsMouseDragging(ImGuiMouseButton_Middle, _))
        .WillByDefault(Invoke([this](ImGuiMouseButton, float) { return dragging; }));
  }

  // Moves the cursor and runs one frame of input handling.
  void DragTo(float x, float y) {
    cursor = ImVec2(x, y);
    canvas.HandleInput();
  }

  NiceMock<MockGui> gui;
  ImGuiIO io;
  ImVec2 cursor{0, 0};
  bool hovered = true;
  bool dragging = false;
  Canvas canvas{{.gui = &gui}};
  Camera camera;
};

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

TEST(CanvasTest, FixedLogicalViewportPreservesGameCoordinatesWhileFittingTheWidget) {
  NiceMock<MockGui> mock_gui;
  ON_CALL(mock_gui, GetCursorScreenPos()).WillByDefault(Return(ImVec2(100, 50)));

  Canvas canvas({
      .gui = &mock_gui,
      .logical_viewport = GameViewSize{.width = 960, .height = 540},
      .show_rulers = false,
  });
  Camera camera{.position = {480, 270}};
  canvas.Begin("TestCanvas", ImVec2(800, 600), camera);

  EXPECT_EQ(camera.viewport_width, 960);
  EXPECT_EQ(camera.viewport_height, 540);
  const ImVec2 center = canvas.WorldToScreen({480, 270});
  const ImVec2 top_left = canvas.WorldToScreen({0, 0});
  const ImVec2 bottom_right = canvas.WorldToScreen({960, 540});
  EXPECT_FLOAT_EQ(center.x, 500);
  EXPECT_FLOAT_EQ(center.y, 350);
  EXPECT_FLOAT_EQ(top_left.x, 100);
  EXPECT_FLOAT_EQ(top_left.y, 125);
  EXPECT_FLOAT_EQ(bottom_right.x, 900);
  EXPECT_FLOAT_EQ(bottom_right.y, 575);
  EXPECT_EQ(canvas.ScreenToWorld({100, 125}), Vec(0, 0));
  const Vec round_trip = canvas.ScreenToWorld({900, 575});
  EXPECT_NEAR(round_trip.x, 960, 0.001);
  EXPECT_NEAR(round_trip.y, 540, 0.001);

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

// Panning was keyboard-only, despite a comment in blueprint_editor.cc implying
// otherwise. Dragging the world is the gesture everyone reaches for first.
TEST(CanvasTest, MiddleDragMovesTheWorldWithTheCursor) {
  PanHarness h;
  h.canvas.SetWorldBounds({0, 0}, {2000, 1000});
  h.canvas.Begin("TestCanvas", ImVec2(800, 600), h.camera);

  const Vec start = h.camera.position;

  // The first frame of a drag only records where the grab began.
  h.dragging = true;
  h.DragTo(500.0f, 400.0f);
  EXPECT_EQ(h.camera.position, start);

  // Dragging left and up pulls the world with the cursor, so the camera moves
  // right and down by the same amount.
  h.DragTo(480.0f, 390.0f);
  EXPECT_DOUBLE_EQ(h.camera.position.x, start.x + 20.0);
  EXPECT_DOUBLE_EQ(h.camera.position.y, start.y + 10.0);

  h.canvas.End();
}

// The grabbed point has to stay under the pointer, or panning feels like it is
// fighting the cursor at anything other than 1:1.
TEST(CanvasTest, MiddleDragTracksTheCursorAtAnyZoom) {
  PanHarness h;
  h.canvas.SetWorldBounds({0, 0}, {2000, 1000});
  h.canvas.Begin("TestCanvas", ImVec2(800, 600), h.camera);
  h.camera.zoom = 2.0f;

  const Vec start = h.camera.position;
  h.dragging = true;
  h.DragTo(500.0f, 400.0f);
  h.DragTo(460.0f, 400.0f);

  EXPECT_DOUBLE_EQ(h.camera.position.x, start.x + 20.0);

  h.canvas.End();
}

// Drag state is global to ImGui, so a canvas that panned on any middle drag
// would move in lockstep with every other viewport on screen.
TEST(CanvasTest, ADragThatBeganElsewhereDoesNotPanThisCanvas) {
  PanHarness h;
  h.canvas.SetWorldBounds({0, 0}, {2000, 1000});
  h.canvas.Begin("TestCanvas", ImVec2(800, 600), h.camera);

  const Vec start = h.camera.position;
  h.hovered = false;
  h.dragging = true;
  h.DragTo(500.0f, 400.0f);
  h.DragTo(400.0f, 300.0f);

  EXPECT_EQ(h.camera.position, start);

  h.canvas.End();
}

// Releasing the button ends the drag. Without that, the next drag would apply
// the whole distance the cursor travelled in between as one jump.
TEST(CanvasTest, ReleasingTheButtonEndsTheDrag) {
  PanHarness h;
  h.canvas.SetWorldBounds({0, 0}, {2000, 1000});
  h.canvas.Begin("TestCanvas", ImVec2(800, 600), h.camera);

  h.dragging = true;
  h.DragTo(500.0f, 400.0f);
  h.DragTo(480.0f, 400.0f);
  const Vec after_first_drag = h.camera.position;

  h.dragging = false;
  h.DragTo(100.0f, 400.0f);
  EXPECT_EQ(h.camera.position, after_first_drag);

  // The next drag starts fresh from wherever the cursor now is.
  h.dragging = true;
  h.DragTo(100.0f, 400.0f);
  EXPECT_EQ(h.camera.position, after_first_drag);

  h.DragTo(90.0f, 400.0f);
  EXPECT_DOUBLE_EQ(h.camera.position.x, after_first_drag.x + 10.0);

  h.canvas.End();
}

TEST(CanvasTest, MiddleDragStaysInsideTheWorldBounds) {
  PanHarness h;
  h.canvas.SetWorldBounds({0, 0}, {2000, 1000});
  h.canvas.Begin("TestCanvas", ImVec2(800, 600), h.camera);

  const Vec start = h.camera.position;
  h.dragging = true;
  h.DragTo(0.0f, 0.0f);
  h.DragTo(5000.0f, 5000.0f);

  // Dragging far past the edge clamps rather than sailing off the world.
  EXPECT_EQ(h.camera.position, start);

  h.canvas.End();
}

}  // namespace
}  // namespace zebes
