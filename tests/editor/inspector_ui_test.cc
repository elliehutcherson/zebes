#include "editor/inspector_ui.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

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

TEST(InspectorInputTest, KeepsIntermediateDoubleOutOfTheCommittedValue) {
  NiceMock<MockGui> gui;
  double value = 5000.0;
  EXPECT_CALL(gui, InputDouble(testing::StrEq("Period X"), _, 0.0, 0.0, testing::StrEq("%.6f"), 0))
      .WillOnce(DoAll(SetArgPointee<1>(6.0), Return(true)));
  EXPECT_CALL(gui, IsItemDeactivatedAfterEdit()).WillOnce(Return(false));

  EXPECT_FALSE(InputCommittedDouble(gui, "Period X", value));
  EXPECT_DOUBLE_EQ(value, 5000.0);
}

TEST(InspectorInputTest, CommitsDoubleWhenEditingFinishes) {
  NiceMock<MockGui> gui;
  double value = 5000.0;
  EXPECT_CALL(gui, InputDouble(testing::StrEq("Period X"), _, 0.0, 0.0, testing::StrEq("%.6f"), 0))
      .WillOnce(DoAll(SetArgPointee<1>(6000.0), Return(true)));
  EXPECT_CALL(gui, IsItemDeactivatedAfterEdit()).WillOnce(Return(true));

  EXPECT_TRUE(InputCommittedDouble(gui, "Period X", value));
  EXPECT_DOUBLE_EQ(value, 6000.0);
}

}  // namespace
}  // namespace zebes
