#include "mock_gui.h"

#include <type_traits>
#include <utility>

#include "editor/gui.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace zebes {
namespace {

using ::testing::Return;
using ::testing::StrEq;

static_assert(std::is_same_v<decltype(std::declval<Gui&>().Begin("window")), bool>);

TEST(MockGuiTest, Instantiate) {
  MockGui mock_gui;
  // Just verify we can instantiate it and it links correctly.
  EXPECT_TRUE(true);
}

TEST(MockGuiTest, ConvenienceOverloadsForwardCanonicalDefaults) {
  MockGui mock_gui;
  GuiInterface& gui = mock_gui;
  double value = 0.0;

  EXPECT_CALL(mock_gui, Begin(StrEq("window"), nullptr, 0)).WillOnce(Return(true));
  EXPECT_CALL(mock_gui, MenuItem(StrEq("save"), nullptr, false, true)).WillOnce(Return(true));
  EXPECT_CALL(mock_gui, InputDouble(StrEq("value"), &value, 0.0, 0.0, StrEq("%.6f"), 0))
      .WillOnce(Return(false));
  EXPECT_CALL(mock_gui, IsKeyPressed(ImGuiKey_Enter, true)).WillOnce(Return(true));
  EXPECT_CALL(mock_gui, ShowMetricsWindow(nullptr));

  EXPECT_TRUE(gui.Begin("window"));
  EXPECT_TRUE(gui.MenuItem("save"));
  EXPECT_FALSE(gui.InputDouble("value", &value));
  EXPECT_TRUE(gui.IsKeyPressed(ImGuiKey_Enter));
  gui.ShowMetricsWindow();
}

}  // namespace
}  // namespace zebes
