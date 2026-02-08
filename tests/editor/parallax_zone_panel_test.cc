#include "editor/level_editor/parallax_zone_panel.h"

#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/level.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"

namespace zebes {

// Peer class to access private members of ParallaxZonePanel
class ParallaxZonePanelTestPeer {
 public:
  using Op = ParallaxZonePanel::Op;

  static absl::Status HandleOp(ParallaxZonePanel& panel, Level& level, Op op) {
    return panel.HandleOp(level, op);
  }

  static int GetSelectedZoneIndex(const ParallaxZonePanel& panel) {
    return panel.selected_zone_index_;
  }

  static void SetSelectedZoneIndex(ParallaxZonePanel& panel, int index) {
    panel.selected_zone_index_ = index;
  }

  static const std::optional<ParallaxZone>& GetEditingZone(const ParallaxZonePanel& panel) {
    return panel.editing_zone_;
  }

  static std::optional<ParallaxZone>& GetEditingZoneMutable(ParallaxZonePanel& panel) {
    return panel.editing_zone_;
  }

  static int GetState(const ParallaxZonePanel& panel) { return static_cast<int>(panel.state_); }
};

namespace {

using ::testing::_;
using ::testing::An;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrEq;

class ParallaxZonePanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    level_.zones.clear();
    level_.themes.clear();

    // Add dummy themes
    level_.themes["Theme1"] = ParallaxTheme{.name = "Theme1"};

    auto panel_or = ParallaxZonePanel::Create({.api = api_.get(), .gui = &gui_});
    ASSERT_TRUE(panel_or.ok());
    panel_ = *std::move(panel_or);

    // Mock IO and Style
    static ImGuiIO io;
    ON_CALL(gui_, GetIO()).WillByDefault(ReturnRef(io));
    static ImGuiStyle style;
    ON_CALL(gui_, GetStyle()).WillByDefault(ReturnRef(style));
    ON_CALL(gui_, GetContentRegionAvail()).WillByDefault(Return(ImVec2(800, 600)));

    // Mock ScopedId
    ON_CALL(gui_, CreateScopedId(::testing::A<const char*>()))
        .WillByDefault(::testing::Invoke([this](const char* id) { return ScopedId(&gui_, id); }));

    // Mock ScopedListBox
    ON_CALL(gui_, CreateScopedListBox(_, _))
        .WillByDefault(::testing::Invoke(
            [this](const char* label, ImVec2 size) { return ScopedListBox(&gui_, label, size); }));
    ON_CALL(gui_, BeginListBox(_, _)).WillByDefault(Return(true));

    // Mock ScopedStyleColor
    ON_CALL(gui_, CreateScopedStyleColor(_, An<ImU32>()))
        .WillByDefault([&](ImGuiCol idx, ImU32 col) { return ScopedStyleColor(&gui_, {}, {}); });
    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(
            [&](ImGuiCol idx, const ImVec4& col) { return ScopedStyleColor(&gui_, {}, {}); });

    // By default, all buttons return false
    ON_CALL(gui_, Button).WillByDefault(Return(false));
  }

  absl::Status HandleOp(ParallaxZonePanelTestPeer::Op op) {
    return ParallaxZonePanelTestPeer::HandleOp(*panel_, level_, op);
  }

  std::unique_ptr<MockApi> api_ = std::make_unique<NiceMock<MockApi>>();
  NiceMock<MockGui> gui_;
  std::unique_ptr<ParallaxZonePanel> panel_;
  Level level_;
};

TEST_F(ParallaxZonePanelTest, CreateZoneAddsToLevel) {
  // Initial state
  EXPECT_TRUE(level_.zones.empty());

  // Create zone
  ASSERT_TRUE(HandleOp(ParallaxZonePanelTestPeer::Op::kCreateZone).ok());

  // Verify zone added
  EXPECT_EQ(level_.zones.size(), 1);
  EXPECT_EQ(ParallaxZonePanelTestPeer::GetSelectedZoneIndex(*panel_), 0);
}

TEST_F(ParallaxZonePanelTest, DeleteZoneRemovesFromLevel) {
  level_.zones.push_back({});
  ParallaxZonePanelTestPeer::SetSelectedZoneIndex(*panel_, 0);

  ASSERT_TRUE(HandleOp(ParallaxZonePanelTestPeer::Op::kDeleteZone).ok());

  EXPECT_TRUE(level_.zones.empty());
  EXPECT_EQ(ParallaxZonePanelTestPeer::GetSelectedZoneIndex(*panel_), -1);
}

TEST_F(ParallaxZonePanelTest, EditZoneStartsEditing) {
  level_.zones.push_back({});
  ParallaxZonePanelTestPeer::SetSelectedZoneIndex(*panel_, 0);

  ASSERT_TRUE(HandleOp(ParallaxZonePanelTestPeer::Op::kEditZone).ok());

  EXPECT_TRUE(ParallaxZonePanelTestPeer::GetEditingZone(*panel_).has_value());
  EXPECT_EQ(ParallaxZonePanelTestPeer::GetState(*panel_), 1);  // kZoneDetails
}

TEST_F(ParallaxZonePanelTest, SaveZoneUpdatesLevel) {
  // Setup
  level_.zones.push_back({});
  ParallaxZonePanelTestPeer::SetSelectedZoneIndex(*panel_, 0);

  // Enter edit mode
  ASSERT_TRUE(HandleOp(ParallaxZonePanelTestPeer::Op::kEditZone).ok());

  // Modify
  auto& editing = ParallaxZonePanelTestPeer::GetEditingZoneMutable(*panel_);
  editing->theme_id = "Theme1";
  editing->min_point = {10, 20};

  // Save
  ASSERT_TRUE(HandleOp(ParallaxZonePanelTestPeer::Op::kSaveZone).ok());

  // Verify
  ASSERT_EQ(level_.zones.size(), 1);
  EXPECT_EQ(level_.zones[0].theme_id, "Theme1");
  EXPECT_EQ(level_.zones[0].min_point.x, 10);
  EXPECT_EQ(level_.zones[0].min_point.y, 20);
}

TEST_F(ParallaxZonePanelTest, BackButtonExitsDetails) {
  level_.zones.push_back({});
  ParallaxZonePanelTestPeer::SetSelectedZoneIndex(*panel_, 0);
  ASSERT_TRUE(HandleOp(ParallaxZonePanelTestPeer::Op::kEditZone).ok());

  // Back
  ASSERT_TRUE(HandleOp(ParallaxZonePanelTestPeer::Op::kBackToZones).ok());

  EXPECT_FALSE(ParallaxZonePanelTestPeer::GetEditingZone(*panel_).has_value());
  EXPECT_EQ(ParallaxZonePanelTestPeer::GetState(*panel_), 0);  // kZoneList
}

TEST_F(ParallaxZonePanelTest, RenderZoneList_EditButton_Transitions) {
  level_.zones.push_back({});
  ParallaxZonePanelTestPeer::SetSelectedZoneIndex(*panel_, 0);

  // Generic Fallback
  EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(gui_, Button(StrEq("Edit Zone"), _)).WillOnce(Return(true));

  auto result = panel_->Render(level_);
  ASSERT_TRUE(result.ok());

  EXPECT_EQ(ParallaxZonePanelTestPeer::GetState(*panel_), 1);  // kZoneDetails
}

}  // namespace
}  // namespace zebes
