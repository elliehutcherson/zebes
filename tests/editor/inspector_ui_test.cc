#include "editor/inspector_ui.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

TEST(InspectorPropertyGridTest, GivesLabelsTheirOwnColumnAndControlsTheRemainingWidth) {
  NiceMock<MockGui> gui;
  InspectorPropertyGrid grid(gui, "LevelIdentity");

  EXPECT_CALL(gui, TableNextRow(_, _));
  EXPECT_CALL(gui, TableNextColumn()).Times(2);
  EXPECT_CALL(gui, SetNextItemWidth(-FLT_MIN));

  EXPECT_TRUE(grid.BeginRow("Name"));
}

TEST(InspectorPropertyGridTest, ShowsFieldMeaningWhenItsLabelIsHovered) {
  NiceMock<MockGui> gui;
  InspectorPropertyGrid grid(gui, "WorldSize");
  EXPECT_CALL(gui, IsItemHovered(_)).WillOnce(Return(true));

  EXPECT_TRUE(grid.BeginRow("Width", "Horizontal world size in pixels."));
}

}  // namespace
}  // namespace zebes
