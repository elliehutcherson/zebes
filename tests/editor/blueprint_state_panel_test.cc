#include "editor/blueprint_editor/blueprint_state_panel.h"

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

class BlueprintStatePanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(panel_, BlueprintStatePanel::Create(&gui_));
    ON_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillByDefault(
            Invoke([this](const char* label, const char* preview, ImGuiComboFlags flags) {
              return ScopedCombo(&gui_, label, preview, flags);
            }));
    ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(false));
  }

  NiceMock<MockGui> gui_;
  std::unique_ptr<BlueprintStatePanel> panel_;
};

TEST(BlueprintStatePanelCreateTest, RequiresGui) {
  EXPECT_EQ(BlueprintStatePanel::Create(nullptr).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(BlueprintStatePanelTest, PlacementModeIsEditable) {
  Blueprint blueprint{.states = {{.key = "default", .name = "Default"}}};
  panel_->SetState(blueprint, 0);
  ON_CALL(gui_, BeginCombo(StrEq("Placement"), StrEq("Grounded"), _)).WillByDefault(Return(true));
  ON_CALL(gui_, Selectable(StrEq("Ceiling"), false, _, _)).WillByDefault(Return(true));

  panel_->Render();

  EXPECT_EQ(blueprint.states.front().placement_mode, BlueprintPlacementMode::kCeiling);
}

}  // namespace
}  // namespace zebes
