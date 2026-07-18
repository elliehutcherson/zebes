#include "editor/level_editor/parallax_theme_panel.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/level.h"
#include "objects/texture.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"

namespace zebes {

class ParallaxThemePanelTestPeer {
 public:
  static absl::Status RenderNavigator(ParallaxThemePanel& panel, Level& level,
                                      SelectionState& selection) {
    return panel.RenderNavigator(level, selection);
  }

  static absl::Status RenderThemeDetails(ParallaxThemePanel& panel, Level& level,
                                         SelectionState& selection) {
    return panel.RenderThemeDetails(level, selection);
  }

  static absl::Status RenderLayerDetails(ParallaxThemePanel& panel, Level& level,
                                         SelectionState& selection) {
    return panel.RenderLayerDetails(level, selection);
  }
};

namespace {

using ::testing::_;
using ::testing::A;
using ::testing::An;
using ::testing::Invoke;
using ::testing::Matcher;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrEq;
using ::testing::Test;

class ParallaxThemePanelTest : public Test {
 protected:
  void SetUp() override {
    level_.themes.clear();

    textures_ = {// ID, Name, Path, SDL_Texture*
                 {"t_001", "grass_ground", "assets/tiles/grass.png", {}},
                 {"t_002", "stone_wall", "assets/tiles/stone.png", {}},
                 {"t_003", "player_idle", "assets/chars/hero.png", {}},
                 {"t_004", "enemy_slime", "assets/chars/slime.png", {}},
                 {"t_005", "ui_button", "assets/ui/btn_ok.png", {}}};

    ON_CALL(*api_, GetAllTextures()).WillByDefault(Return(textures_));

    auto panel_or = ParallaxThemePanel::Create({.api = api_.get(), .gui = &gui_});
    ASSERT_TRUE(panel_or.ok());
    panel_ = *std::move(panel_or);

    // Mock IO and Style
    static ImGuiIO io;
    ON_CALL(gui_, GetIO()).WillByDefault(ReturnRef(io));
    static ImGuiStyle style;
    ON_CALL(gui_, GetStyle()).WillByDefault(ReturnRef(style));
    ON_CALL(gui_, GetContentRegionAvail()).WillByDefault(Return(ImVec2(800, 600)));

    // Mock ScopedId
    ON_CALL(gui_, CreateScopedId(A<const char*>())).WillByDefault(Invoke([this](const char* id) {
      return ScopedId(&gui_, id);
    }));
    ON_CALL(gui_, CreateScopedId(A<int>())).WillByDefault(Invoke([this](int id) {
      return ScopedId(&gui_, id);
    }));

    // Mock ScopedListBox
    ON_CALL(gui_, CreateScopedListBox(_, _))
        .WillByDefault(Invoke(
            [this](const char* label, ImVec2 size) { return ScopedListBox(&gui_, label, size); }));
    ON_CALL(gui_, BeginListBox(_, _)).WillByDefault(Return(true));

    // Mock ScopedStyleColor
    ON_CALL(gui_, CreateScopedStyleColor(_, An<ImU32>()))
        .WillByDefault([&](ImGuiCol idx, ImU32 col) { return ScopedStyleColor(&gui_, {}, {}); });
    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(
            [&](ImGuiCol idx, const ImVec4& col) { return ScopedStyleColor(&gui_, {}, {}); });

    EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));
  }

  absl::Status RenderNavigator() {
    return ParallaxThemePanelTestPeer::RenderNavigator(*panel_, level_, selection_);
  }

  absl::Status RenderThemeDetails() {
    return ParallaxThemePanelTestPeer::RenderThemeDetails(*panel_, level_, selection_);
  }

  absl::Status RenderLayerDetails() {
    return ParallaxThemePanelTestPeer::RenderLayerDetails(*panel_, level_, selection_);
  }

  std::unique_ptr<MockApi> api_ = std::make_unique<NiceMock<MockApi>>();
  NiceMock<MockGui> gui_;
  std::unique_ptr<ParallaxThemePanel> panel_;
  Level level_;
  std::vector<Texture> textures_;
  SelectionState selection_;
};

TEST_F(ParallaxThemePanelTest, AddThemeUpdatesLevelAndSelection) {
  EXPECT_TRUE(level_.themes.empty());

  EXPECT_CALL(gui_, Button(StrEq("Add Theme"), _)).WillOnce(Return(true));

  ASSERT_TRUE(RenderNavigator().ok());

  EXPECT_EQ(level_.themes.size(), 1);
  EXPECT_EQ(selection_.type, SelectionState::Type::kTheme);
  EXPECT_EQ(selection_.theme_id, 0);
  EXPECT_TRUE(level_.themes.contains(0));
}

TEST_F(ParallaxThemePanelTest, AddLayerUpdatesThemeAndSelection) {
  level_.themes[1] = ParallaxTheme{.name = "Theme 1"};
  selection_.type = SelectionState::Type::kTheme;
  selection_.theme_id = 1;

  EXPECT_CALL(gui_, Button(StrEq("Add Layer"), _)).WillOnce(Return(true));

  ASSERT_TRUE(RenderThemeDetails().ok());

  EXPECT_EQ(level_.themes[1].layers.size(), 1);
  EXPECT_EQ(selection_.type, SelectionState::Type::kLayer);
  EXPECT_EQ(selection_.theme_id, 1);
  EXPECT_EQ(selection_.layer_index, 0);
}

TEST_F(ParallaxThemePanelTest, DeleteThemeRemovesIt) {
  level_.themes[1] = ParallaxTheme{.name = "Theme 1"};
  selection_.type = SelectionState::Type::kTheme;
  selection_.theme_id = 1;

  EXPECT_CALL(gui_, Button(StrEq("Delete Theme"), _)).WillOnce(Return(true));

  ASSERT_TRUE(RenderThemeDetails().ok());

  EXPECT_TRUE(level_.themes.empty());
  EXPECT_EQ(selection_.type, SelectionState::Type::kNone);
}

TEST_F(ParallaxThemePanelTest, DeleteLayerRemovesIt) {
  ParallaxTheme theme{.name = "Theme 1"};
  theme.layers.push_back(ParallaxLayer{.name = "Layer 0"});
  level_.themes[1] = theme;

  selection_.type = SelectionState::Type::kLayer;
  selection_.theme_id = 1;
  selection_.layer_index = 0;

  EXPECT_CALL(gui_, CreateScopedCombo(_, _, _))
      .WillRepeatedly(Invoke([this](const char* label, const char* preview, ImGuiComboFlags flags) {
        return ScopedCombo(&gui_, label, preview, flags);
      }));

  EXPECT_CALL(gui_, Button(StrEq("Delete Layer"), _)).WillOnce(Return(true));

  ASSERT_TRUE(RenderLayerDetails().ok());

  EXPECT_CALL(gui_, CreateScopedCombo(_, _, _))
      .WillRepeatedly(Invoke([this](const char* label, const char* preview, ImGuiComboFlags flags) {
        return ScopedCombo(&gui_, label, preview, flags);
      }));

  EXPECT_TRUE(level_.themes[1].layers.empty());
  // The refactored code falls back to the parent theme instead of kNone
  EXPECT_EQ(selection_.type, SelectionState::Type::kTheme);
}

TEST_F(ParallaxThemePanelTest, SelectionChangesOnTreeNode) {
  level_.themes[1] = ParallaxTheme{.name = "Theme 1"};

  // Updated to CollapsingHeader to match parallax_theme_panel.cc
  EXPECT_CALL(gui_, CollapsingHeader(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(gui_, CollapsingHeader(StrEq("Theme 1"), _)).WillOnce(Return(false));
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));

  ASSERT_TRUE(RenderNavigator().ok());

  EXPECT_EQ(selection_.type, SelectionState::Type::kTheme);
  EXPECT_EQ(selection_.theme_id, 1);
}

TEST_F(ParallaxThemePanelTest, SelectionChangesOnLayerSelect) {
  ParallaxTheme theme{.name = "Theme 1"};
  theme.layers.push_back(ParallaxLayer{.name = "Layer 0"});
  level_.themes[1] = theme;

  EXPECT_CALL(gui_, CollapsingHeader(_, _)).WillRepeatedly(Return(false));
  // Must return true here so the implementation steps into the layer loop
  EXPECT_CALL(gui_, CollapsingHeader(StrEq("Theme 1"), _)).WillOnce(Return(true));

  // Use Matcher<bool> to fix ambiguity, and add ##0 for ImGui unique IDs
  EXPECT_CALL(gui_, Selectable(StrEq("Layer 0##0"), Matcher<bool>(false), _, _))
      .WillOnce(Return(true));

  ASSERT_TRUE(RenderNavigator().ok());

  EXPECT_EQ(selection_.type, SelectionState::Type::kLayer);
  EXPECT_EQ(selection_.theme_id, 1);
  EXPECT_EQ(selection_.layer_index, 0);
}

// Renamed slightly to reflect the new fallback behavior
TEST_F(ParallaxThemePanelTest, RenderLayerDetailsFallsBackToThemeOnInvalidSelection) {
  level_.themes[1] = ParallaxTheme{.name = "Theme 1"};
  selection_.type = SelectionState::Type::kLayer;
  selection_.theme_id = 1;
  selection_.layer_index = 5;

  // Now returns OkStatus() but modifies selection to kTheme
  EXPECT_TRUE(RenderLayerDetails().ok());
  EXPECT_EQ(selection_.type, SelectionState::Type::kTheme);
}

}  // namespace
}  // namespace zebes
