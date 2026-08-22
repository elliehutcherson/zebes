#include "editor/level_editor/blueprint_palette_panel.h"

#include <memory>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/blueprint.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"

namespace zebes {

class BlueprintPalettePanelTestPeer {
 public:
  static absl::Status Render(BlueprintPalettePanel& panel) { return panel.Render(); }

  static void SetSearchQuery(BlueprintPalettePanel& panel, std::string query) {
    panel.model_.SetSearchQuery(std::move(query));
  }

  static const std::optional<std::string>& SelectedId(const BlueprintPalettePanel& panel) {
    return panel.model_.selected_blueprint_id();
  }
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
    ASSERT_OK(panel_or);
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

    ON_CALL(gui_, CreateScopedId(::testing::A<const char*>()))
        .WillByDefault(Invoke([this](const char* id) { return ScopedId(&gui_, id); }));

    // Checkbox: return false by default (no toggle)
    ON_CALL(gui_, Checkbox(_, _)).WillByDefault(Return(false));

    ON_CALL(gui_, IsItemClicked(_)).WillByDefault(Return(false));
    ON_CALL(gui_, IsItemHovered(_)).WillByDefault(Return(false));

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

TEST_F(BlueprintPalettePanelTest, EmptyBlueprintListRendersWithoutError) {
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{}));

  EXPECT_OK(Render());
  EXPECT_EQ(panel_->GetSelectedBlueprint(), nullptr);
}

TEST_F(BlueprintPalettePanelTest, ThumbnailClickSelectsBlueprint) {
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{stable_bp_}));
  ON_CALL(*api_, GetBlueprint("bp-abc")).WillByDefault(Return(&stable_bp_));

  EXPECT_CALL(gui_, InvisibleButton(HasSubstr("blueprint"), _, _)).Times(1);
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));

  ASSERT_OK(Render());

  ASSERT_TRUE(BlueprintPalettePanelTestPeer::SelectedId(*panel_).has_value());
  EXPECT_EQ(*BlueprintPalettePanelTestPeer::SelectedId(*panel_), "bp-abc");
  ASSERT_NE(panel_->GetSelectedBlueprint(), nullptr);
  EXPECT_EQ(panel_->GetSelectedBlueprint()->id, "bp-abc");
}

TEST_F(BlueprintPalettePanelTest, SelectionResolvesFreshApiStorageByStableId) {
  Blueprint refreshed = stable_bp_;
  refreshed.name = "Refreshed Samus";
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{stable_bp_}));
  ON_CALL(*api_, GetBlueprint("bp-abc")).WillByDefault(Return(&refreshed));

  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));
  ASSERT_OK(Render());

  ASSERT_NE(panel_->GetSelectedBlueprint(), nullptr);
  EXPECT_EQ(panel_->GetSelectedBlueprint(), &refreshed);
}

TEST_F(BlueprintPalettePanelTest, ClearSelectionResetsToNull) {
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{stable_bp_}));
  ON_CALL(*api_, GetBlueprint("bp-abc")).WillByDefault(Return(&stable_bp_));

  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));
  ASSERT_OK(Render());
  ASSERT_NE(panel_->GetSelectedBlueprint(), nullptr);

  panel_->ClearSelection();
  EXPECT_EQ(panel_->GetSelectedBlueprint(), nullptr);
}

TEST_F(BlueprintPalettePanelTest, ClickingSelectedBlueprintTogglesOff) {
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{stable_bp_}));
  ON_CALL(*api_, GetBlueprint("bp-abc")).WillByDefault(Return(&stable_bp_));

  // First click: select
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));
  ASSERT_OK(Render());
  ASSERT_NE(panel_->GetSelectedBlueprint(), nullptr);

  // Second click on same blueprint: deselect
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));
  ASSERT_OK(Render());
  EXPECT_EQ(panel_->GetSelectedBlueprint(), nullptr);
}

TEST_F(BlueprintPalettePanelTest, SearchFiltersTheThumbnailGridByName) {
  const Blueprint crystal{.id = "crystal", .name = "Cave Crystal"};
  ON_CALL(*api_, GetAllBlueprints())
      .WillByDefault(Return(std::vector<Blueprint>{stable_bp_, crystal}));
  BlueprintPalettePanelTestPeer::SetSearchQuery(*panel_, "crYsTaL");

  EXPECT_CALL(gui_, InvisibleButton(HasSubstr("blueprint"), _, _)).Times(1);
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(false));

  ASSERT_OK(Render());
}

TEST_F(BlueprintPalettePanelTest, ResolvesFirstStateSpriteForThumbnail) {
  Sprite sprite{
      .id = "crystal-sprite",
      .frames = {SpriteFrame{.texture_w = 32, .texture_h = 64, .render_w = 32, .render_h = 64}},
  };
  stable_bp_.states = {{.name = "Default", .sprite_id = sprite.id}};
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{stable_bp_}));
  EXPECT_CALL(*api_, GetSprite(sprite.id)).WillOnce(Return(&sprite));

  ASSERT_OK(Render());
}

TEST_F(BlueprintPalettePanelTest, CheckboxTogglesSnapToGrid) {
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{stable_bp_}));

  // By default, snap_to_grid_ is true.
  EXPECT_TRUE(panel_->GetSnapToGrid());

  EXPECT_CALL(gui_, Checkbox("Snap to Grid", _)).WillOnce(Invoke([](const char* label, bool* v) {
    *v = false;  // toggle it to false
    return true;
  }));
  EXPECT_CALL(gui_, Checkbox("Show Entity Borders", _)).WillOnce(Return(false));

  ASSERT_OK(Render());
  EXPECT_FALSE(panel_->GetSnapToGrid());
}

TEST_F(BlueprintPalettePanelTest, CheckboxTogglesShowEntityBorders) {
  ON_CALL(*api_, GetAllBlueprints()).WillByDefault(Return(std::vector<Blueprint>{stable_bp_}));

  // By default, show_entity_borders_ is false.
  EXPECT_FALSE(panel_->GetShowEntityBorders());

  EXPECT_CALL(gui_, Checkbox("Snap to Grid", _)).WillOnce(Return(false));
  EXPECT_CALL(gui_, Checkbox("Show Entity Borders", _))
      .WillOnce(Invoke([](const char* label, bool* v) {
        *v = true;  // toggle it to true
        return true;
      }));

  ASSERT_OK(Render());
  EXPECT_TRUE(panel_->GetShowEntityBorders());
}

}  // namespace
}  // namespace zebes
