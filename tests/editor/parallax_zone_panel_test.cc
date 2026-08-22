#include "editor/level_editor/parallax_zone_panel.h"

#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
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
    themes_ = {{.id = "theme-1", .name = "Theme1", .layers = {}}};
    ON_CALL(api_, GetAllParallaxThemes()).WillByDefault(Return(themes_));

    auto panel_or = ParallaxZonePanel::Create({.api = &api_, .gui = &gui_});
    ASSERT_OK(panel_or);
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

  NiceMock<MockGui> gui_;
  NiceMock<MockApi> api_;
  std::unique_ptr<ParallaxZonePanel> panel_;
  std::vector<ParallaxTheme> themes_;
  Level level_;
  SelectionState selection_;
};

TEST_F(ParallaxZonePanelTest, CreateZoneAddsToLevel) {
  level_.width = 1024.0;
  level_.height = 512.0;

  // Initial state
  EXPECT_TRUE(level_.zones.empty());

  EXPECT_CALL(gui_, Button(StrEq("Add Zone"), _)).WillOnce(Return(true));

  // Create zone
  ASSERT_OK(RenderNavigator());

  // Verify zone added and selected
  ASSERT_EQ(level_.zones.size(), 1);
  EXPECT_EQ(selection_.type, SelectionState::Type::kZone);
  EXPECT_EQ(selection_.zone_id, 0);
  EXPECT_EQ(level_.zones[0].name, "Zone 0");
  EXPECT_EQ(level_.zones[0].id, 0);
  EXPECT_DOUBLE_EQ(level_.zones[0].max_point.x, 256.0);
  EXPECT_DOUBLE_EQ(level_.zones[0].max_point.y, 256.0);
  EXPECT_EQ(level_.zones[0].fade_length, Vec());
}

TEST_F(ParallaxZonePanelTest, CreateZoneIsDisabledForLevelWithoutPositiveDimensions) {
  level_.width = 0.0;
  level_.height = 512.0;

  // A disabled ImGui button cannot return true in production. Returning true
  // here also verifies that the model-side guard preserves the invariant.
  EXPECT_CALL(gui_, Button(StrEq("Add Zone"), _)).WillOnce(Return(true));

  ASSERT_OK(RenderNavigator());

  EXPECT_TRUE(level_.zones.empty());
  EXPECT_EQ(selection_.type, SelectionState::Type::kNone);
}

TEST_F(ParallaxZonePanelTest, DeleteZoneRemovesFromLevel) {
  level_.zones.push_back({});
  selection_.type = SelectionState::Type::kZone;
  selection_.zone_id = 0;

  EXPECT_CALL(gui_, Button(StrEq("Delete Zone"), _)).WillOnce(Return(true));

  ASSERT_OK(RenderDetails());

  EXPECT_TRUE(level_.zones.empty());
  EXPECT_EQ(selection_.type, SelectionState::Type::kNone);
}

TEST_F(ParallaxZonePanelTest, SelectionStateUpdatedOnSelect) {
  ParallaxZone zone;
  zone.id = 0;
  zone.name = "My Zone";
  level_.zones.push_back(zone);

  EXPECT_CALL(gui_, Selectable(StrEq("My Zone##zone_0"), false, _, _)).WillOnce(Return(true));

  ASSERT_OK(RenderNavigator());

  EXPECT_EQ(selection_.type, SelectionState::Type::kZone);
  EXPECT_EQ(selection_.zone_id, 0);
}

TEST_F(ParallaxZonePanelTest, CreateZoneAssignsUniqueIds) {
  level_.width = 1024.0;
  level_.height = 512.0;

  // Add two zones via the Add Zone button
  EXPECT_CALL(gui_, Button(StrEq("Add Zone"), _))
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));
  ASSERT_OK(RenderNavigator());

  EXPECT_CALL(gui_, Button(StrEq("Add Zone"), _))
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));
  ASSERT_OK(RenderNavigator());

  ASSERT_EQ(level_.zones.size(), 2);
  EXPECT_NE(level_.zones[0].id, level_.zones[1].id);
  EXPECT_FALSE(level_.zones[0].name.empty());
  EXPECT_FALSE(level_.zones[1].name.empty());
}

TEST_F(ParallaxZonePanelTest, DetailsReturnsErrorOnInvalidId) {
  level_.zones.push_back({});
  selection_.type = SelectionState::Type::kZone;
  selection_.zone_id = 5;  // Invalid

  ASSERT_FALSE(RenderDetails().ok());

  // Selection should be cleared on error
  EXPECT_EQ(selection_.type, SelectionState::Type::kNone);
}

TEST_F(ParallaxZonePanelTest, SelectionUsesStableIdAfterZoneOrderChanges) {
  level_.zones = {
      {.id = 10,
       .name = "First",
       .theme_id = "theme-1",
       .min_point = {0, 0},
       .max_point = {50, 50}},
      {.id = 20,
       .name = "Second",
       .theme_id = "theme-1",
       .min_point = {50, 0},
       .max_point = {100, 50}},
  };
  selection_.type = SelectionState::Type::kZone;
  selection_.zone_id = 20;

  level_.zones.erase(level_.zones.begin());

  ASSERT_OK(RenderDetails());
  EXPECT_EQ(selection_.zone_id, 20);
}

TEST_F(ParallaxZonePanelTest, NavigatorShowsThemeNameInLabel) {
  ParallaxZone zone;
  zone.id = 0;
  zone.name = "My Zone";
  zone.theme_id = "theme-1";
  level_.zones.push_back(zone);

  EXPECT_CALL(gui_, Selectable(StrEq("My Zone (Theme1)##zone_0"), false, _, _))
      .WillOnce(Return(false));

  ASSERT_OK(RenderNavigator());
}

TEST_F(ParallaxZonePanelTest, NavigatorUsesSafeLabelsForEmptyNames) {
  themes_[0].name.clear();
  ON_CALL(api_, GetAllParallaxThemes()).WillByDefault(Return(themes_));
  level_.zones.push_back(ParallaxZone{.id = 4, .name = "", .theme_id = "theme-1"});

  EXPECT_CALL(gui_, Selectable(StrEq("(unnamed zone) (unnamed theme)##zone_4"), false, _, _))
      .WillOnce(Return(false));

  EXPECT_OK(RenderNavigator());
}

TEST_F(ParallaxZonePanelTest, ComboPreviewShowsSelectedTheme) {
  ParallaxZone zone;
  zone.id = 0;
  zone.name = "My Zone";
  zone.theme_id = "theme-1";
  level_.zones.push_back(zone);
  selection_.type = SelectionState::Type::kZone;
  selection_.zone_id = 0;

  EXPECT_CALL(gui_, CreateScopedCombo(StrEq("Theme"), StrEq("Theme1"), _))
      .WillOnce(Invoke([this](const char* label, const char* preview, ImGuiComboFlags flags) {
        return ScopedCombo(&gui_, label, preview, flags);
      }));

  ASSERT_OK(RenderDetails());
}

TEST_F(ParallaxZonePanelTest, EditThemeEmitsStableIdRequest) {
  ParallaxZone zone{
      .id = 0,
      .name = "Zone 0",
      .theme_id = "theme-1",
      .min_point = {0, 0},
      .max_point = {100, 100},
  };
  level_.zones.push_back(zone);
  selection_.type = SelectionState::Type::kZone;
  selection_.zone_id = 0;

  EXPECT_CALL(gui_, Button(StrEq("Edit Theme"), _)).WillOnce(Return(true));

  ASSERT_OK(RenderDetails());
  std::optional<ParallaxZonePanel::ThemeRequest> request = panel_->TakeThemeRequest();
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->action, ParallaxZonePanel::ThemeAction::kEdit);
  EXPECT_EQ(request->zone_id, 0);
  EXPECT_EQ(request->theme_id, "theme-1");
}

TEST_F(ParallaxZonePanelTest, UnsupportedFadeCanBeResetWithoutImplyingItRenders) {
  level_.zones.push_back({
      .id = 0,
      .name = "Zone 0",
      .theme_id = "theme-1",
      .min_point = {0, 0},
      .max_point = {100, 100},
      .fade_length = {10, 15},
  });
  selection_.type = SelectionState::Type::kZone;
  selection_.zone_id = 0;
  EXPECT_CALL(gui_, Button(StrEq("Reset Fades to Zero"), _)).WillOnce(Return(true));

  ASSERT_OK(RenderDetails());

  EXPECT_EQ(level_.zones[0].fade_length, Vec());
}

}  // namespace
}  // namespace zebes
