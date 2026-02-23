#include "editor/canvas/canvas.h"

#include <gtest/gtest.h>

#include "objects/camera.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

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

}  // namespace
}  // namespace zebes
