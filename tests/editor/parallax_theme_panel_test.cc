#include "editor/level_editor/parallax_theme_panel.h"

#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/level.h"
#include "objects/texture.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"

namespace zebes {

// Peer class to access private members of ParallaxThemePanel
class ParallaxThemePanelTestPeer {
 public:
  using Op = ParallaxThemePanel::Op;

  static absl::Status HandleOp(ParallaxThemePanel& panel, Level& level, Op op) {
    return panel.HandleOp(level, op);
  }

  static void SetSelectedThemeName(ParallaxThemePanel& panel, const std::string& name) {
    panel.selected_theme_name_ = name;
  }

  static const std::string& GetSelectedThemeName(const ParallaxThemePanel& panel) {
    return panel.selected_theme_name_;
  }

  static int GetSelectedLayerIndex(const ParallaxThemePanel& panel) {
    return panel.selected_layer_index_;
  }

  static void SetSelectedLayerIndex(ParallaxThemePanel& panel, int index) {
    panel.selected_layer_index_ = index;
  }

  static const std::optional<ParallaxLayer>& GetEditingLayer(const ParallaxThemePanel& panel) {
    return panel.editing_layer_;
  }

  static std::optional<ParallaxLayer>& GetEditingLayerMutable(ParallaxThemePanel& panel) {
    return panel.editing_layer_;
  }

  static void SetSelectedTextureIndex(ParallaxThemePanel& panel, int index) {
    panel.selected_texture_index_ = index;
  }

  static int GetSelectedTextureIndex(const ParallaxThemePanel& panel) {
    return panel.selected_texture_index_;
  }

  static int GetState(const ParallaxThemePanel& panel) { return static_cast<int>(panel.state_); }

  static void SetEditingThemeName(ParallaxThemePanel& panel, const std::string& name) {
    panel.editing_theme_name_ = name;
  }
};

namespace {

using ::testing::_;
using ::testing::An;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrEq;

class ParallaxThemePanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    level_.themes.clear();

    textures_ = {// ID, Name, Path, SDL_Texture*
                 {"t_001", "grass_ground", "assets/tiles/grass.png", nullptr},
                 {"t_002", "stone_wall", "assets/tiles/stone.png", nullptr},
                 {"t_003", "player_idle", "assets/chars/hero.png", nullptr},
                 {"t_004", "enemy_slime", "assets/chars/slime.png", nullptr},
                 {"t_005", "ui_button", "assets/ui/btn_ok.png", nullptr}};

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
    ON_CALL(gui_, CreateScopedId(::testing::A<const char*>()))
        .WillByDefault(::testing::Invoke([this](const char* id) { return ScopedId(&gui_, id); }));
    ON_CALL(gui_, CreateScopedId(::testing::A<int>()))
        .WillByDefault(::testing::Invoke([this](int id) { return ScopedId(&gui_, id); }));

    // Mock ScopedListBox
    ON_CALL(gui_, CreateScopedListBox(_, _))
        .WillByDefault(::testing::Invoke(
            [this](const char* label, ImVec2 size) { return ScopedListBox(&gui_, label, size); }));
    // Allow ListBox to "open" -> requires BeginListBox to return true
    ON_CALL(gui_, BeginListBox(_, _)).WillByDefault(Return(true));

    // Mock ScopedStyleColor
    ON_CALL(gui_, CreateScopedStyleColor(_, An<ImU32>()))
        .WillByDefault([&](ImGuiCol idx, ImU32 col) { return ScopedStyleColor(&gui_, {}, {}); });
    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(
            [&](ImGuiCol idx, const ImVec4& col) { return ScopedStyleColor(&gui_, {}, {}); });

    // By default, all button calls shoudl return false
    ON_CALL(gui_, Button).WillByDefault(Return(false));
  }

  absl::Status HandleOp(ParallaxThemePanelTestPeer::Op op) {
    return ParallaxThemePanelTestPeer::HandleOp(*panel_, level_, op);
  }

  std::unique_ptr<MockApi> api_ = std::make_unique<NiceMock<MockApi>>();
  NiceMock<MockGui> gui_;
  std::unique_ptr<ParallaxThemePanel> panel_;
  Level level_;
  std::vector<Texture> textures_;
};

TEST_F(ParallaxThemePanelTest, CreateThemeAddsToLevel) {
  // Initial state
  EXPECT_TRUE(level_.themes.empty());

  // Create theme
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kCreateTheme).ok());

  // Verify theme added
  EXPECT_EQ(level_.themes.size(), 1);
  EXPECT_TRUE(level_.themes.contains("Theme 0"));
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetSelectedThemeName(*panel_), "Theme 0");
}

TEST_F(ParallaxThemePanelTest, CreateThemeHandlesCollisions) {
  // Create first theme
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kCreateTheme).ok());

  // Create another theme - should be "Theme 1"
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kCreateTheme).ok());
  EXPECT_TRUE(level_.themes.contains("Theme 1"));

  // Force create "Theme 2" manually in map
  level_.themes["Theme 2"] = ParallaxTheme{.name = "Theme 2"};

  // Create via panel - should detect collision and append random suffix?
  // Wait, the logic is `StrCat("Theme ", level.themes.size())` -> "Theme 3".
  // So "Theme 2" exists, size is 3. Next is "Theme 3". No collision.

  // Let's manually create "Theme 3" to cause collision
  level_.themes["Theme 3"] = ParallaxTheme{.name = "Theme 3"};
  // Now size is 4. HandleOp tries "Theme 4".

  // To trigger collision, we need map size to produce a name that exists.
  // e.g. We have "Theme 0". Map size 1. Next is "Theme 1".
  // If we rename "Theme 0" to "Theme 1" manually?
  level_.themes.clear();
  level_.themes["Theme 1"] = ParallaxTheme{.name = "Theme 1"};
  // Size is 1. Next try "Theme 1". Collision!

  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kCreateTheme).ok());
  EXPECT_EQ(level_.themes.size(), 2);
  // Peer should select the new one
  std::string selected = ParallaxThemePanelTestPeer::GetSelectedThemeName(*panel_);
  EXPECT_NE(selected, "Theme 1");
  EXPECT_THAT(selected, testing::StartsWith("Theme 1_"));
}

TEST_F(ParallaxThemePanelTest, DeleteThemeRemovesFromLevel) {
  level_.themes["MyTheme"] = ParallaxTheme{.name = "MyTheme"};
  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "MyTheme");

  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kDeleteTheme).ok());

  EXPECT_TRUE(level_.themes.empty());
  EXPECT_TRUE(ParallaxThemePanelTestPeer::GetSelectedThemeName(*panel_).empty());
}

TEST_F(ParallaxThemePanelTest, SelectThemeTransitionsState) {
  level_.themes["MyTheme"] = ParallaxTheme{.name = "MyTheme"};
  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "MyTheme");

  // Select Op
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kEditTheme).ok());

  // State transition verification is hard without exposing state enum,
  // but check side effects: selected_layer_index should be reset.
  ParallaxThemePanelTestPeer::SetSelectedLayerIndex(*panel_, 5);
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kEditTheme).ok());
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetSelectedLayerIndex(*panel_), -1);
}

TEST_F(ParallaxThemePanelTest, CreateLayerAddsToTheme) {
  level_.themes["MyTheme"] = ParallaxTheme{.name = "MyTheme"};
  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "MyTheme");

  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kCreateLayer).ok());

  EXPECT_EQ(level_.themes["MyTheme"].layers.size(), 1);
  EXPECT_EQ(level_.themes["MyTheme"].layers[0].name, "Layer 0");
  // Should select the new layer
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetSelectedLayerIndex(*panel_), 0);
}

TEST_F(ParallaxThemePanelTest, SelectLayerStartsEditing) {
  ParallaxTheme theme;
  theme.name = "MyTheme";
  theme.layers.push_back({.name = "Layer 0", .texture_id = "t_001"});
  level_.themes["MyTheme"] = theme;
  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "MyTheme");

  // Select index 0
  ParallaxThemePanelTestPeer::SetSelectedLayerIndex(*panel_, 0);

  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kEditLayer).ok());

  auto editing = ParallaxThemePanelTestPeer::GetEditingLayer(*panel_);
  ASSERT_TRUE(editing.has_value());
  EXPECT_EQ(editing->name, "Layer 0");
  EXPECT_EQ(editing->texture_id, "t_001");
}

TEST_F(ParallaxThemePanelTest, SaveLayerUpdatesTheme) {
  // Setup
  ParallaxTheme theme;
  theme.name = "MyTheme";
  theme.layers.push_back({.name = "Layer 0", .texture_id = "t_001"});
  level_.themes["MyTheme"] = theme;
  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "MyTheme");
  ParallaxThemePanelTestPeer::SetSelectedLayerIndex(*panel_, 0);

  // Enter edit mode
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kEditLayer).ok());

  // Modify
  auto& editing = ParallaxThemePanelTestPeer::GetEditingLayerMutable(*panel_);
  ASSERT_TRUE(editing.has_value());
  editing->name = "Updated Layer";
  editing->texture_id = "t_002";

  // Save
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kSaveLayer).ok());

  // Verify
  const auto& layers = level_.themes["MyTheme"].layers;
  ASSERT_EQ(layers.size(), 1);
  EXPECT_EQ(layers[0].name, "Updated Layer");
  EXPECT_EQ(layers[0].texture_id, "t_002");
}

TEST_F(ParallaxThemePanelTest, SaveLayerValidation) {
  // Setup
  ParallaxTheme theme;
  theme.name = "MyTheme";
  theme.layers.push_back({.name = "Layer 0", .texture_id = "t_001"});
  level_.themes["MyTheme"] = theme;
  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "MyTheme");
  ParallaxThemePanelTestPeer::SetSelectedLayerIndex(*panel_, 0);
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kEditLayer).ok());

  // 1. Empty Name
  auto& editing = ParallaxThemePanelTestPeer::GetEditingLayerMutable(*panel_);
  std::string original = editing->name;
  editing->name = "";

  auto result = HandleOp(ParallaxThemePanelTestPeer::Op::kSaveLayer);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.message(), "Layer name cannot be empty");

  // 2. Empty Texture
  editing->name = original;
  editing->texture_id = "";
  result = HandleOp(ParallaxThemePanelTestPeer::Op::kSaveLayer);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.message(), "Layer texture must be selected");
}

TEST_F(ParallaxThemePanelTest, DeleteLayerRemovesFromTheme) {
  ParallaxTheme theme;
  theme.name = "MyTheme";
  theme.layers.push_back({.name = "Layer 0"});
  level_.themes["MyTheme"] = theme;
  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "MyTheme");

  // Select to delete
  ParallaxThemePanelTestPeer::SetSelectedLayerIndex(*panel_, 0);

  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kDeleteLayer).ok());

  EXPECT_TRUE(level_.themes["MyTheme"].layers.empty());
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetSelectedLayerIndex(*panel_), -1);
}

TEST_F(ParallaxThemePanelTest, TextureSelectionWorkflow) {
  // Setup theme and layer
  ParallaxTheme theme;
  theme.name = "MyTheme";
  theme.layers.push_back({.name = "Layer 0", .texture_id = ""});
  level_.themes["MyTheme"] = theme;
  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "MyTheme");
  ParallaxThemePanelTestPeer::SetSelectedLayerIndex(*panel_, 0);

  // Enter Edit
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kEditLayer).ok());

  // Select texture index 2 ("t_003" / "player_idle" - assuming cache sorted by name?)
  // Cache:
  // enemy_slime (t_004)
  // grass_ground (t_001)
  // player_idle (t_003)  <-- Index 2
  // stone_wall (t_002)
  // ui_button (t_005)

  ParallaxThemePanelTestPeer::SetSelectedTextureIndex(*panel_, 2);

  // Apply texture
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kChangeLayerTexture).ok());

  // Verify
  auto& editing = ParallaxThemePanelTestPeer::GetEditingLayerMutable(*panel_);
  EXPECT_EQ(editing->texture_id, "t_003");  // player_idle
}

TEST_F(ParallaxThemePanelTest, BackButtonExitsLayerDetailsWithoutError) {
  // Setup: Add a layer and select it to enter details mode
  level_.themes["Theme1"] = ParallaxTheme{.name = "Theme1"};
  level_.themes["Theme1"].layers.push_back(ParallaxLayer{.name = "Layer1"});

  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "Theme1");
  ParallaxThemePanelTestPeer::SetSelectedLayerIndex(*panel_, 0);

  // Enter RenderLayerDetails state
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kEditLayer).ok());

  // Expect "Back" button to be pressed
  EXPECT_CALL(gui_, Button("Back", _)).WillOnce(Return(true));

  // Call Render
  // Before fix: This would return InternalError("No editing layer")
  // After fix: This should return OkStatus
  auto result = panel_->Render(level_);

  ASSERT_TRUE(result.ok()) << "Render failed with: " << result.status();

  // Verify internal state is effectively back to list (editing layer cleared)
  EXPECT_FALSE(ParallaxThemePanelTestPeer::GetEditingLayer(*panel_).has_value());
}

TEST_F(ParallaxThemePanelTest, RenderThemeList_SelectionUpdatesName_ButNoTransition) {
  level_.themes["T1"] = ParallaxTheme{.name = "T1"};
  // Ensure nothing selected initially
  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "");

  // Mock UI interaction: User clicks "T1"
  // Note: Selectable("T1", ...) returns true
  EXPECT_CALL(gui_, Selectable(StrEq("T1"), ::testing::A<bool>(), _, _)).WillOnce(Return(true));

  // Render
  auto result = panel_->Render(level_);
  ASSERT_TRUE(result.ok());

  // Verify Name Updated
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetSelectedThemeName(*panel_), "T1");

  // Verify State is STILL kThemeList (0)
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetState(*panel_), 0);
}

TEST_F(ParallaxThemePanelTest, RenderThemeList_EditButton_Transitions) {
  level_.themes["T1"] = ParallaxTheme{.name = "T1"};
  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "T1");

  auto state = ParallaxThemePanelTestPeer::GetState(*panel_);
  LOG(INFO) << "state = " << state;

  // 1. Generic Fallback: Allow any other button calls, return false
  EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));

  // Mock UI interaction: User clicks "Edit Theme"
  // Note: Button("Edit Theme", ...) returns true
  EXPECT_CALL(gui_, Button(StrEq("Edit Theme"), _)).WillOnce(Return(true));

  // Render
  LOG(INFO) << "before render...";
  auto result = panel_->Render(level_);
  ASSERT_TRUE(result.ok());

  // Verify State TRANSITIONED to kLayerList (1)
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetState(*panel_), 1);
}

TEST_F(ParallaxThemePanelTest, RenderLayerList_SelectionUpdatesIndex_ButNoTransition) {
  ParallaxTheme theme;
  theme.name = "T1";
  theme.layers.push_back({.name = "L0"});
  level_.themes["T1"] = theme;

  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "T1");
  // Set state to kLayerList (1) manually to test that view
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kEditTheme).ok());
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetState(*panel_), 1);

  // Ensure no layer selected
  ParallaxThemePanelTestPeer::SetSelectedLayerIndex(*panel_, -1);

  // Mock UI interaction: User clicks "L0"
  EXPECT_CALL(gui_, Selectable(StrEq("L0"), ::testing::A<bool>(), _, _)).WillOnce(Return(true));

  // Render
  auto result = panel_->Render(level_);
  ASSERT_TRUE(result.ok());

  // Verify Index Updated
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetSelectedLayerIndex(*panel_), 0);

  // Verify State is STILL kLayerList (1)
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetState(*panel_), 1);
}

TEST_F(ParallaxThemePanelTest, RenderLayerList_EditButton_Transitions) {
  ParallaxTheme theme;
  theme.name = "T1";
  theme.layers.push_back({.name = "L0"});
  level_.themes["T1"] = theme;

  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "T1");
  // Set state to kLayerList (1)
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kEditTheme).ok());

  ParallaxThemePanelTestPeer::SetSelectedLayerIndex(*panel_, 0);

  // Generic Fallback: Allow any other button calls, return false
  EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));

  // Mock UI interaction: User clicks "Edit Layer"
  EXPECT_CALL(gui_, Button(StrEq("Edit Layer"), _)).WillOnce(Return(true));

  // Render
  auto result = panel_->Render(level_);
  ASSERT_TRUE(result.ok()) << result.status();

  // Verify State TRANSITIONED to kLayerDetails (2)
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetState(*panel_), 2);
}

TEST_F(ParallaxThemePanelTest, ThemeRenameUpdatesMap) {
  // Setup
  level_.themes["OldName"] = ParallaxTheme{.name = "OldName"};
  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "OldName");

  // Enter edit mode
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kEditTheme).ok());

  // Simulate editing name
  ParallaxThemePanelTestPeer::SetEditingThemeName(*panel_, "NewName");

  // Commit rename
  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kRenameTheme).ok());

  // Verify
  EXPECT_FALSE(level_.themes.contains("OldName"));
  EXPECT_TRUE(level_.themes.contains("NewName"));
  EXPECT_EQ(level_.themes["NewName"].name, "NewName");
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetSelectedThemeName(*panel_), "NewName");
}

TEST_F(ParallaxThemePanelTest, ThemeRenameCollisionFails) {
  level_.themes["A"] = ParallaxTheme{.name = "A"};
  level_.themes["B"] = ParallaxTheme{.name = "B"};
  ParallaxThemePanelTestPeer::SetSelectedThemeName(*panel_, "A");

  ASSERT_TRUE(HandleOp(ParallaxThemePanelTestPeer::Op::kEditTheme).ok());

  // Try to rename A -> B
  ParallaxThemePanelTestPeer::SetEditingThemeName(*panel_, "B");

  auto status = HandleOp(ParallaxThemePanelTestPeer::Op::kRenameTheme);
  EXPECT_EQ(status.code(), absl::StatusCode::kAlreadyExists);

  // Verify no change
  EXPECT_TRUE(level_.themes.contains("A"));
  EXPECT_EQ(level_.themes["A"].name, "A");
  EXPECT_EQ(ParallaxThemePanelTestPeer::GetSelectedThemeName(*panel_), "A");
}
}  // namespace

}  // namespace zebes
