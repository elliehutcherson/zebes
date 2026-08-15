#include "editor/level_editor/level_panel.h"

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrEq;

TEST(LevelPanelCreateTest, FailsWithNullGui) { EXPECT_FALSE(LevelPanel::Create(nullptr).ok()); }

class LevelPanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto panel = LevelPanel::Create(&gui_);
    ASSERT_TRUE(panel.ok());
    panel_ = *std::move(panel);

    static ImGuiIO io;
    ON_CALL(gui_, GetIO()).WillByDefault(ReturnRef(io));
    static ImGuiStyle style;
    ON_CALL(gui_, GetStyle()).WillByDefault(ReturnRef(style));
    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(
            [&](ImGuiCol index, const ImVec4& color) { return ScopedStyleColor(&gui_, {}, {}); });
    ON_CALL(gui_, CreateScopedListBox(_, _))
        .WillByDefault(Invoke([this](const char* label, ImVec2 size) {
          return ScopedListBox(&gui_, label, size);
        }));
    ON_CALL(gui_, BeginListBox(_, _)).WillByDefault(Return(true));
    ON_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillByDefault(Invoke([this](const char* label, const char* preview, ImGuiComboFlags) {
          return ScopedCombo(&gui_, label, preview);
        }));
    ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(false));
    ON_CALL(gui_, CreateScopedId(An<const char*>()))
        .WillByDefault(Invoke([this](const char* id) { return ScopedId(&gui_, id); }));
    EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));
  }

  // Opens the tileset combo so its entries are drawn this frame.
  void OpenTilesetCombo() { ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(true)); }

  NiceMock<MockGui> gui_;
  LevelPanelModel model_;
  std::unique_ptr<LevelPanel> panel_;
};

TEST_F(LevelPanelTest, RenderListShowsOrderedLevelNames) {
  model_.SetLevels(
      {{.id = "z-id", .name = "Zebra"}, {.id = "a-id", .name = "Alpha"}});

  EXPECT_CALL(gui_, Selectable(StrEq("Alpha##level_a-id"), false, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(gui_, Selectable(StrEq("Zebra##level_z-id"), false, _, _))
      .WillOnce(Return(false));

  absl::StatusOr<LevelPanelEvent> event = panel_->RenderList(model_);
  ASSERT_TRUE(event.ok());
  EXPECT_EQ(event->action, LevelPanelAction::kNone);
}

TEST_F(LevelPanelTest, RenderListUsesSafeLabelForEmptyLevelName) {
  model_.SetLevels({{.id = "level-id", .name = ""}});

  EXPECT_CALL(gui_, Selectable(StrEq("(unnamed level)##level_level-id"), false, _, _))
      .WillOnce(Return(false));

  EXPECT_TRUE(panel_->RenderList(model_).ok());
}

TEST_F(LevelPanelTest, CreateBeginsDraftAndReportsPersistenceIntent) {
  EXPECT_CALL(gui_, Button(StrEq("Create"), _)).WillOnce(Return(true));

  absl::StatusOr<LevelPanelEvent> event = panel_->RenderList(model_);

  ASSERT_TRUE(event.ok());
  EXPECT_EQ(event->action, LevelPanelAction::kCreate);
  ASSERT_NE(model_.active_level(), nullptr);
  EXPECT_TRUE(model_.is_new_level());
}

TEST_F(LevelPanelTest, EditOpensSelectedCatalogLevel) {
  model_.SetLevels({{.id = "alpha", .name = "Alpha"}});
  ASSERT_TRUE(model_.SelectLevel("alpha").ok());
  EXPECT_CALL(gui_, Button(StrEq("Edit"), _)).WillOnce(Return(true));

  absl::StatusOr<LevelPanelEvent> event = panel_->RenderList(model_);

  ASSERT_TRUE(event.ok());
  EXPECT_EQ(event->action, LevelPanelAction::kOpen);
  ASSERT_NE(model_.active_level(), nullptr);
  EXPECT_EQ(model_.active_level()->id, "alpha");
}

TEST_F(LevelPanelTest, SaveReportsIntentWithoutPersisting) {
  model_.BeginEditingLevel(Level{.id = "alpha", .name = "Alpha"});
  EXPECT_CALL(gui_, Button(StrEq("Save"), _)).WillOnce(Return(true));

  absl::StatusOr<LevelPanelEvent> event = panel_->RenderDetails(model_);

  ASSERT_TRUE(event.ok());
  EXPECT_EQ(event->action, LevelPanelAction::kSave);
  EXPECT_TRUE(model_.has_active_level());
}

TEST_F(LevelPanelTest, BackClosesActiveLevel) {
  model_.BeginEditingLevel(Level{.id = "alpha", .name = "Alpha"});
  EXPECT_CALL(gui_, Button(StrEq("Back"), _)).WillOnce(Return(true));

  absl::StatusOr<LevelPanelEvent> event = panel_->RenderDetails(model_);

  ASSERT_TRUE(event.ok());
  EXPECT_EQ(event->action, LevelPanelAction::kClose);
  EXPECT_FALSE(model_.has_active_level());
}

TEST_F(LevelPanelTest, TilesetComboPreviewsTheLevelsTilesetByName) {
  model_.SetTilesetChoices({{.id = "grass-uuid", .name = "Grass"}});
  model_.BeginEditingLevel(Level{.id = "alpha", .tileset_id = "grass-uuid"});

  EXPECT_CALL(gui_, CreateScopedCombo(StrEq("Tileset"), StrEq("Grass"), _));

  EXPECT_TRUE(panel_->RenderDetails(model_).ok());
}

TEST_F(LevelPanelTest, AnUnboundLevelPreviewsNone) {
  model_.SetTilesetChoices({{.id = "grass-uuid", .name = "Grass"}});
  model_.BeginEditingLevel(Level{.id = "alpha"});

  EXPECT_CALL(gui_, CreateScopedCombo(StrEq("Tileset"), StrEq("(none)"), _));

  EXPECT_TRUE(panel_->RenderDetails(model_).ok());
}

TEST_F(LevelPanelTest, PickingATilesetRebindsAnEmptyLevel) {
  model_.SetTilesetChoices({{.id = "grass-uuid", .name = "Grass"}});
  model_.BeginEditingLevel(Level{.id = "alpha", .tileset_id = "sunny-uuid"});
  OpenTilesetCombo();
  EXPECT_CALL(gui_, Selectable(StrEq("Grass"), false, _, _)).WillOnce(Return(true));

  ASSERT_TRUE(panel_->RenderDetails(model_).ok());

  EXPECT_EQ(model_.active_level()->tileset_id, "grass-uuid");
  EXPECT_FALSE(model_.has_pending_tileset_change());
}

TEST_F(LevelPanelTest, PickingATilesetForAPopulatedLevelAsksFirst) {
  model_.SetTilesetChoices({{.id = "grass-uuid", .name = "Grass"}});
  Level level{.id = "alpha", .tileset_id = "sunny-uuid"};
  TileChunk chunk{};
  chunk.tiles[0] = 4;
  level.tile_chunks[0] = chunk;
  model_.BeginEditingLevel(std::move(level));
  OpenTilesetCombo();
  EXPECT_CALL(gui_, Selectable(StrEq("Grass"), false, _, _)).WillOnce(Return(true));
  EXPECT_CALL(gui_, Button(StrEq("Discard tiles and switch"), _)).WillOnce(Return(false));

  ASSERT_TRUE(panel_->RenderDetails(model_).ok());

  EXPECT_TRUE(model_.has_pending_tileset_change());
  EXPECT_EQ(model_.active_level()->tileset_id, "sunny-uuid");
}

TEST_F(LevelPanelTest, ConfirmingTheSwitchDiscardsTheTiles) {
  model_.SetTilesetChoices({{.id = "grass-uuid", .name = "Grass"}});
  Level level{.id = "alpha", .tileset_id = "sunny-uuid"};
  TileChunk chunk{};
  chunk.tiles[0] = 4;
  level.tile_chunks[0] = chunk;
  model_.BeginEditingLevel(std::move(level));
  ASSERT_TRUE(model_.RequestTilesetChange("grass-uuid").ok());
  ASSERT_TRUE(model_.has_pending_tileset_change());

  EXPECT_CALL(gui_, Button(StrEq("Discard tiles and switch"), _)).WillOnce(Return(true));

  ASSERT_TRUE(panel_->RenderDetails(model_).ok());

  EXPECT_EQ(model_.active_level()->tileset_id, "grass-uuid");
  EXPECT_EQ(model_.placed_tile_count(), 0);
}

TEST_F(LevelPanelTest, KeepingTheTilesetCancelsTheSwitch) {
  model_.SetTilesetChoices({{.id = "grass-uuid", .name = "Grass"}});
  Level level{.id = "alpha", .tileset_id = "sunny-uuid"};
  TileChunk chunk{};
  chunk.tiles[0] = 4;
  level.tile_chunks[0] = chunk;
  model_.BeginEditingLevel(std::move(level));
  ASSERT_TRUE(model_.RequestTilesetChange("grass-uuid").ok());

  EXPECT_CALL(gui_, Button(StrEq("Discard tiles and switch"), _)).WillOnce(Return(false));
  EXPECT_CALL(gui_, Button(StrEq("Keep tileset"), _)).WillOnce(Return(true));

  ASSERT_TRUE(panel_->RenderDetails(model_).ok());

  EXPECT_FALSE(model_.has_pending_tileset_change());
  EXPECT_EQ(model_.active_level()->tileset_id, "sunny-uuid");
  EXPECT_EQ(model_.placed_tile_count(), 1);
}

// No confirmation is drawn when nothing is staged, so the row cannot be
// clicked by accident on a later frame.
TEST_F(LevelPanelTest, NoConfirmationWithoutAPendingChange) {
  model_.SetTilesetChoices({{.id = "grass-uuid", .name = "Grass"}});
  model_.BeginEditingLevel(Level{.id = "alpha", .tileset_id = "grass-uuid"});

  EXPECT_CALL(gui_, Button(StrEq("Discard tiles and switch"), _)).Times(0);

  EXPECT_TRUE(panel_->RenderDetails(model_).ok());
}

}  // namespace
}  // namespace zebes
