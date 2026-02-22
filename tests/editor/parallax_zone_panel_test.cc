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
  static absl::Status RenderNavigator(ParallaxZonePanel& panel, Level& level,
                                      SelectionState& selection) {
    return panel.RenderNavigator(level, selection);
  }

  static absl::Status RenderDetails(ParallaxZonePanel& panel, Level& level,
                                    SelectionState& selection) {
    return panel.RenderDetails(level, selection);
  }
};

namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Invoke;
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
    level_.themes[1] = ParallaxTheme{.name = "Theme1"};

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
    EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillRepeatedly(::testing::Invoke(
            [this](const char* label, const char* preview, ImGuiComboFlags flags) {
              return ScopedCombo(&gui_, label, preview, flags);
            }));
  }

  absl::Status RenderNavigator() {
    return ParallaxZonePanelTestPeer::RenderNavigator(*panel_, level_, selection_);
  }

  absl::Status RenderDetails() {
    return ParallaxZonePanelTestPeer::RenderDetails(*panel_, level_, selection_);
  }

  std::unique_ptr<MockApi> api_ = std::make_unique<NiceMock<MockApi>>();
  NiceMock<MockGui> gui_;
  std::unique_ptr<ParallaxZonePanel> panel_;
  Level level_;
  SelectionState selection_;
};

TEST_F(ParallaxZonePanelTest, CreateZoneAddsToLevel) {
  // Initial state
  EXPECT_TRUE(level_.zones.empty());

  EXPECT_CALL(gui_, Button(StrEq("Add Zone"), _)).WillOnce(Return(true));

  // Create zone
  ASSERT_TRUE(RenderNavigator().ok());

  // Verify zone added and selected
  ASSERT_EQ(level_.zones.size(), 1);
  EXPECT_EQ(selection_.type, SelectionState::Type::kZone);
  EXPECT_EQ(selection_.zone_index, 0);
  EXPECT_EQ(level_.zones[0].name, "Zone 0");
  EXPECT_EQ(level_.zones[0].id, 0);
}

TEST_F(ParallaxZonePanelTest, DeleteZoneRemovesFromLevel) {
  level_.zones.push_back({});
  selection_.type = SelectionState::Type::kZone;
  selection_.zone_index = 0;

  EXPECT_CALL(gui_, Button(StrEq("Delete Zone"), _)).WillOnce(Return(true));

  ASSERT_TRUE(RenderDetails().ok());

  EXPECT_TRUE(level_.zones.empty());
  EXPECT_EQ(selection_.type, SelectionState::Type::kNone);
}

TEST_F(ParallaxZonePanelTest, SelectionStateUpdatedOnSelect) {
  ParallaxZone zone;
  zone.id = 0;
  zone.name = "My Zone";
  level_.zones.push_back(zone);

  EXPECT_CALL(gui_, Selectable(StrEq("My Zone##zone_0"), false, _, _)).WillOnce(Return(true));

  ASSERT_TRUE(RenderNavigator().ok());

  EXPECT_EQ(selection_.type, SelectionState::Type::kZone);
  EXPECT_EQ(selection_.zone_index, 0);
}

TEST_F(ParallaxZonePanelTest, CreateZone_AssignsUniqueIds) {
  // Add two zones via the Add Zone button
  EXPECT_CALL(gui_, Button(StrEq("Add Zone"), _))
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));
  ASSERT_TRUE(RenderNavigator().ok());

  EXPECT_CALL(gui_, Button(StrEq("Add Zone"), _))
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));
  ASSERT_TRUE(RenderNavigator().ok());

  ASSERT_EQ(level_.zones.size(), 2);
  EXPECT_NE(level_.zones[0].id, level_.zones[1].id);
  EXPECT_FALSE(level_.zones[0].name.empty());
  EXPECT_FALSE(level_.zones[1].name.empty());
}

TEST_F(ParallaxZonePanelTest, DetailsReturnsErrorOnInvalidIndex) {
  level_.zones.push_back({});
  selection_.type = SelectionState::Type::kZone;
  selection_.zone_index = 5;  // Invalid

  ASSERT_FALSE(RenderDetails().ok());

  // Selection should be cleared on error
  EXPECT_EQ(selection_.type, SelectionState::Type::kNone);
}

TEST_F(ParallaxZonePanelTest, NavigatorShowsThemeNameInLabel) {
  ParallaxZone zone;
  zone.id = 0;
  zone.name = "My Zone";
  zone.theme_id = 1;  // Matches the theme added in SetUp
  level_.zones.push_back(zone);

  EXPECT_CALL(gui_, Selectable(StrEq("My Zone##zone_0 (Theme1)"), false, _, _))
      .WillOnce(Return(false));

  ASSERT_TRUE(RenderNavigator().ok());
}

TEST_F(ParallaxZonePanelTest, ComboPreviewShowsSelectedTheme) {
  ParallaxZone zone;
  zone.id = 0;
  zone.name = "My Zone";
  zone.theme_id = 1;
  level_.zones.push_back(zone);
  selection_.type = SelectionState::Type::kZone;
  selection_.zone_index = 0;

  EXPECT_CALL(gui_, CreateScopedCombo(StrEq("Theme"), StrEq("Theme1"), _))
      .WillOnce(Invoke([this](const char* label, const char* preview, ImGuiComboFlags flags) {
        return ScopedCombo(&gui_, label, preview, flags);
      }));

  ASSERT_TRUE(RenderDetails().ok());
}

TEST_F(ParallaxZonePanelTest, RenderDetails_ShowsLayerOffsets) {
  ParallaxLayer layer{
      .name = "Trees",
      .texture_id = "tex_trees",
      .offset = {250.0, -100.0},
  };
  ParallaxTheme theme{
      .id = 2,
      .name = "Forest",
      .layers = {layer},
  };
  level_.themes[2] = theme;

  ParallaxZone zone{
      .id = 0,
      .name = "Zone 0",
      .theme_id = 2,
      .min_point = {0, 0},
      .max_point = {100, 100},
  };
  level_.zones.push_back(zone);
  selection_.type = SelectionState::Type::kZone;
  selection_.zone_index = 0;

  EXPECT_CALL(gui_, InputDouble).WillRepeatedly(Return(false));

  ASSERT_TRUE(RenderDetails().ok());
}

}  // namespace
}  // namespace zebes
