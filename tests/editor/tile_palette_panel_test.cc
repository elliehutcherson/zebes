#include "editor/level_editor/tile_palette_panel.h"

#include <memory>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/tileset.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"

namespace zebes {

class TilePalettePanelTestPeer {
 public:
  static const Tileset* GetSelectedTileset(const TilePalettePanel& p) {
    return p.selected_tileset_;
  }
  static void SetSelectedTileset(TilePalettePanel& p, const Tileset* ts) {
    p.selected_tileset_ = ts;
  }
};

namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

class TilePalettePanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto panel_or = TilePalettePanel::Create({.api = api_.get(), .gui = &gui_});
    ASSERT_TRUE(panel_or.ok());
    panel_ = *std::move(panel_or);

    // Combo: closed by default.
    ON_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillByDefault(Invoke([this](const char* label, const char* preview, ImGuiComboFlags) {
          return ScopedCombo(&gui_, label, preview);
        }));
    ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(false));

    // Child: open by default (for TileGrid).
    ON_CALL(gui_, CreateScopedChild(_, _, _, _))
        .WillByDefault(
            Invoke([this](const char* id, ImVec2 size, bool border, ImGuiWindowFlags flags) {
              return ScopedChild(&gui_, id, size, border, flags);
            }));
    ON_CALL(gui_, BeginChild(_, _, _, _)).WillByDefault(Return(true));

    ON_CALL(gui_, GetContentRegionAvail()).WillByDefault(Return(ImVec2(400, 150)));
    ON_CALL(gui_, GetCursorScreenPos()).WillByDefault(Return(ImVec2(0, 0)));
    ON_CALL(gui_, GetWindowDrawList()).WillByDefault(Return(nullptr));

    ON_CALL(gui_, InvisibleButton(_, _, _)).WillByDefault(Return(false));
    ON_CALL(gui_, IsItemClicked(_)).WillByDefault(Return(false));
    ON_CALL(gui_, IsItemHovered(_)).WillByDefault(Return(false));

    ON_CALL(gui_, Checkbox(_, _)).WillByDefault(Return(false));
    ON_CALL(gui_, SameLine(_, _)).WillByDefault(Return());
    ON_CALL(gui_, CreateScopedId(An<int>()))
        .WillByDefault(Invoke([this](int id) { return ScopedId(&gui_, id); }));
  }

  absl::Status Render() { return panel_->Render(16, 16); }

  std::unique_ptr<NiceMock<MockApi>> api_ = std::make_unique<NiceMock<MockApi>>();
  NiceMock<MockGui> gui_;
  std::unique_ptr<TilePalettePanel> panel_;

  // Stable tileset storage for pointer tests.
  Tileset stable_ts_{.id = "ts-1", .name = "Ground", .tile_width = 16, .tile_height = 16};
  Tile stable_tile_{.id = 1, .name = "Grass"};
};

// --- Create validation ---

TEST(TilePalettePanelCreateTest, FailsWithNullApi) {
  NiceMock<MockGui> gui;
  auto result = TilePalettePanel::Create({.api = nullptr, .gui = &gui});
  EXPECT_FALSE(result.ok());
}

TEST(TilePalettePanelCreateTest, FailsWithNullGui) {
  NiceMock<MockApi> api;
  auto result = TilePalettePanel::Create({.api = &api, .gui = nullptr});
  EXPECT_FALSE(result.ok());
}

// --- Render: no tilesets ---

TEST_F(TilePalettePanelTest, Render_NoTilesets_SucceedsAndSelectionIsNull) {
  ON_CALL(*api_, GetAllTilesets()).WillByDefault(Return(std::vector<Tileset>{}));
  ASSERT_TRUE(Render().ok());
  EXPECT_EQ(panel_->GetSelectedTile(), nullptr);
  EXPECT_EQ(panel_->GetSelectedTileset(), nullptr);
}

// --- Checkbox defaults ---

TEST_F(TilePalettePanelTest, GetShowTileFrame_DefaultTrue) {
  EXPECT_TRUE(panel_->GetShowTileFrame());
}

TEST_F(TilePalettePanelTest, GetShowTileCollision_DefaultFalse) {
  EXPECT_FALSE(panel_->GetShowTileCollision());
}

// --- Tileset selection via peer injection ---

TEST_F(TilePalettePanelTest, Render_WithTilesetPreSelected_RendersGrid) {
  stable_ts_.tiles = {stable_tile_};
  ON_CALL(*api_, GetAllTilesets()).WillByDefault(Return(std::vector<Tileset>{stable_ts_}));
  // Inject tileset directly to bypass the combo's Selectable overload ambiguity.
  TilePalettePanelTestPeer::SetSelectedTileset(*panel_, &stable_ts_);
  ASSERT_TRUE(Render().ok());
  EXPECT_EQ(panel_->GetSelectedTileset(), &stable_ts_);
}

// --- Tile click selection ---

TEST_F(TilePalettePanelTest, Render_ClickTile_SelectsTile) {
  stable_ts_.tiles = {stable_tile_};
  TilePalettePanelTestPeer::SetSelectedTileset(*panel_, &stable_ts_);
  ON_CALL(*api_, GetAllTilesets()).WillByDefault(Return(std::vector<Tileset>{stable_ts_}));
  ON_CALL(*api_, GetTileset("ts-1")).WillByDefault(Return(&stable_ts_));

  // Simulate a click on the tile button.
  EXPECT_CALL(gui_, InvisibleButton(_, _, _)).WillOnce(Return(false));
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));

  ASSERT_TRUE(Render().ok());
  ASSERT_NE(panel_->GetSelectedTile(), nullptr);
  EXPECT_EQ(panel_->GetSelectedTile()->id, 1);
}

TEST_F(TilePalettePanelTest, Render_ClickSameTile_Deselects) {
  stable_ts_.tiles = {stable_tile_};
  TilePalettePanelTestPeer::SetSelectedTileset(*panel_, &stable_ts_);
  ON_CALL(*api_, GetAllTilesets()).WillByDefault(Return(std::vector<Tileset>{stable_ts_}));
  ON_CALL(*api_, GetTileset("ts-1")).WillByDefault(Return(&stable_ts_));

  // First click: select.
  EXPECT_CALL(gui_, InvisibleButton(_, _, _)).WillOnce(Return(false));
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));
  ASSERT_TRUE(Render().ok());
  ASSERT_NE(panel_->GetSelectedTile(), nullptr);

  // Second click on same tile: deselect.
  EXPECT_CALL(gui_, InvisibleButton(_, _, _)).WillOnce(Return(false));
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));
  ASSERT_TRUE(Render().ok());
  EXPECT_EQ(panel_->GetSelectedTile(), nullptr);
}

// --- ClearSelection ---

TEST_F(TilePalettePanelTest, ClearSelection_ResetsSelectedTile) {
  stable_ts_.tiles = {stable_tile_};
  TilePalettePanelTestPeer::SetSelectedTileset(*panel_, &stable_ts_);
  ON_CALL(*api_, GetAllTilesets()).WillByDefault(Return(std::vector<Tileset>{stable_ts_}));
  ON_CALL(*api_, GetTileset("ts-1")).WillByDefault(Return(&stable_ts_));

  EXPECT_CALL(gui_, InvisibleButton(_, _, _)).WillOnce(Return(false));
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));
  ASSERT_TRUE(Render().ok());
  ASSERT_NE(panel_->GetSelectedTile(), nullptr);

  panel_->ClearSelection();
  EXPECT_EQ(panel_->GetSelectedTile(), nullptr);
}

}  // namespace
}  // namespace zebes
