#include "editor/level_editor/blueprint_palette_panel.h"

#include <memory>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/blueprint.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"

namespace zebes {

class BlueprintPalettePanelTestPeer {
 public:
  static absl::Status Render(BlueprintPalettePanel& panel) { return panel.Render(); }
};

namespace {

using ::testing::_;
using ::testing::An;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

class BlueprintPalettePanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto panel_or = BlueprintPalettePanel::Create({.api = api_.get(), .gui = &gui_});
    ASSERT_TRUE(panel_or.ok());
    panel_ = *std::move(panel_or);

    // ScopedChild: open by default
    ON_CALL(gui_, CreateScopedChild(_, _, _, _))
        .WillByDefault(
            Invoke([this](const char* id, ImVec2 size, bool border, ImGuiWindowFlags flags) {
              return ScopedChild(&gui_, id, size, border, flags);
            }));
    ON_CALL(gui_, BeginChild(_, _, _, _)).WillByDefault(Return(true));

    // ScopedStyleColor: no-op
    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(
            [&](ImGuiCol idx, const ImVec4& col) { return ScopedStyleColor(&gui_, {}, {}); });

    // Checkbox: return false by default (no toggle)
    ON_CALL(gui_, Checkbox(_, _)).WillByDefault(Return(false));

    // Button: return false by default (no click)
    ON_CALL(gui_, Button(_, _)).WillByDefault(Return(false));

    // SameLine: no-op
    ON_CALL(gui_, SameLine(_, _)).WillByDefault(Return());
  }

  absl::Status Render() { return BlueprintPalettePanelTestPeer::Render(*panel_); }

  std::unique_ptr<NiceMock<MockApi>> api_ = std::make_unique<NiceMock<MockApi>>();
  NiceMock<MockGui> gui_;
  std::unique_ptr<BlueprintPalettePanel> panel_;

  // Stable blueprint storage for pointer tests.
  Blueprint stable_bp_{.id = "bp-abc", .name = "Samus"};
};

TEST_F(BlueprintPalettePanelTest, EmptyBlueprintList_RendersWithoutError) {
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{}));

  EXPECT_TRUE(Render().ok());
  EXPECT_EQ(panel_->GetSelectedBlueprint(), nullptr);
}

TEST_F(BlueprintPalettePanelTest, ButtonClick_SelectsBlueprint) {
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{stable_bp_}));
  ON_CALL(*api_, GetBlueprint("bp-abc")).WillByDefault(Return(&stable_bp_));

  EXPECT_CALL(gui_, Button(HasSubstr("Samus"), _)).WillOnce(Return(true));

  ASSERT_TRUE(Render().ok());

  ASSERT_NE(panel_->GetSelectedBlueprint(), nullptr);
  EXPECT_EQ(panel_->GetSelectedBlueprint()->id, "bp-abc");
}

TEST_F(BlueprintPalettePanelTest, ClearSelection_ResetsToNull) {
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{stable_bp_}));
  ON_CALL(*api_, GetBlueprint("bp-abc")).WillByDefault(Return(&stable_bp_));

  EXPECT_CALL(gui_, Button(HasSubstr("Samus"), _)).WillOnce(Return(true));
  ASSERT_TRUE(Render().ok());
  ASSERT_NE(panel_->GetSelectedBlueprint(), nullptr);

  panel_->ClearSelection();
  EXPECT_EQ(panel_->GetSelectedBlueprint(), nullptr);
}

TEST_F(BlueprintPalettePanelTest, ClickingSelectedBlueprint_TogglesOff) {
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{stable_bp_}));
  ON_CALL(*api_, GetBlueprint("bp-abc")).WillByDefault(Return(&stable_bp_));

  // First click: select
  EXPECT_CALL(gui_, Button(HasSubstr("Samus"), _)).WillOnce(Return(true));
  ASSERT_TRUE(Render().ok());
  ASSERT_NE(panel_->GetSelectedBlueprint(), nullptr);

  // Second click on same blueprint: deselect
  EXPECT_CALL(gui_, Button(HasSubstr("Samus"), _)).WillOnce(Return(true));
  ASSERT_TRUE(Render().ok());
  EXPECT_EQ(panel_->GetSelectedBlueprint(), nullptr);
}

TEST_F(BlueprintPalettePanelTest, Checkbox_TogglesSnapToGrid) {
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{stable_bp_}));

  // By default, snap_to_grid_ is true.
  EXPECT_TRUE(panel_->GetSnapToGrid());

  EXPECT_CALL(gui_, Checkbox("Snap to Grid", _)).WillOnce(Invoke([](const char* label, bool* v) {
    *v = false;  // toggle it to false
    return true;
  }));
  EXPECT_CALL(gui_, Checkbox("Show Entity Borders", _)).WillOnce(Return(false));

  ASSERT_TRUE(Render().ok());
  EXPECT_FALSE(panel_->GetSnapToGrid());
}

TEST_F(BlueprintPalettePanelTest, Checkbox_TogglesShowEntityBorders) {
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{stable_bp_}));

  // By default, show_entity_borders_ is false.
  EXPECT_FALSE(panel_->GetShowEntityBorders());

  EXPECT_CALL(gui_, Checkbox("Snap to Grid", _)).WillOnce(Return(false));
  EXPECT_CALL(gui_, Checkbox("Show Entity Borders", _))
      .WillOnce(Invoke([](const char* label, bool* v) {
        *v = true;  // toggle it to true
        return true;
      }));

  ASSERT_TRUE(Render().ok());
  EXPECT_TRUE(panel_->GetShowEntityBorders());
}

}  // namespace
}  // namespace zebes
