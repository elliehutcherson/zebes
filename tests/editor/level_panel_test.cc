#include "editor/level_editor/level_panel.h"

#include <memory>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/level.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"

namespace zebes {

class LevelPanelTestPeer {
 public:
  static void SetSelectedIndex(LevelPanel& panel, int index) {
    panel.selected_index_ = index;
  }

  static const std::vector<Level>& GetLevelCache(const LevelPanel& panel) {
    return panel.level_cache_;
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

// Standalone Create validation tests (no shared fixture needed).
TEST(LevelPanelCreateTest, FailsWithNullApi) {
  NiceMock<MockGui> gui;
  EXPECT_FALSE(LevelPanel::Create({.api = nullptr, .gui = &gui}).ok());
}

TEST(LevelPanelCreateTest, FailsWithNullGui) {
  NiceMock<MockApi> api;
  EXPECT_FALSE(LevelPanel::Create({.api = &api, .gui = nullptr}).ok());
}

class LevelPanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Panel constructor calls RefreshCache → GetAllLevels.
    // NiceMock returns empty vector by default.
    auto panel_or = LevelPanel::Create({.api = api_.get(), .gui = &gui_});
    ASSERT_TRUE(panel_or.ok());
    panel_ = *std::move(panel_or);

    // GUI defaults
    static ImGuiIO io;
    ON_CALL(gui_, GetIO()).WillByDefault(ReturnRef(io));
    static ImGuiStyle style;
    ON_CALL(gui_, GetStyle()).WillByDefault(ReturnRef(style));

    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(
            [&](ImGuiCol idx, const ImVec4& col) { return ScopedStyleColor(&gui_, {}, {}); });
    ON_CALL(gui_, CreateScopedListBox(_, _))
        .WillByDefault(Invoke([this](const char* label, ImVec2 size) {
          return ScopedListBox(&gui_, label, size);
        }));
    ON_CALL(gui_, BeginListBox(_, _)).WillByDefault(Return(true));

    // All buttons return false by default; individual tests override as needed.
    EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));
  }

  std::unique_ptr<MockApi> api_ = std::make_unique<NiceMock<MockApi>>();
  NiceMock<MockGui> gui_;
  std::unique_ptr<LevelPanel> panel_;
  std::optional<Level> level_;
  SelectionState selection_;
};

// --- HandleOp::kLevelCreate ---

TEST_F(LevelPanelTest, HandleOp_Create_AddsLevelAndSetsSelection) {
  EXPECT_CALL(*api_, CreateLevel(_)).WillOnce(Return(std::string("new-id")));

  ASSERT_TRUE(panel_->HandleOp(level_, selection_, LevelPanel::Op::kLevelCreate).ok());

  ASSERT_TRUE(level_.has_value());
  EXPECT_EQ(level_->id, "new-id");
  EXPECT_EQ(selection_.type, SelectionState::Type::kLevel);
}

// --- HandleOp::kLevelEdit ---

TEST_F(LevelPanelTest, HandleOp_Edit_FailsIfLevelAlreadyLoaded) {
  level_ = Level{.id = "existing"};

  EXPECT_FALSE(panel_->HandleOp(level_, selection_, LevelPanel::Op::kLevelEdit).ok());
}

TEST_F(LevelPanelTest, HandleOp_Edit_NoopIfNothingSelected) {
  // selected_index_ defaults to -1.
  ASSERT_TRUE(panel_->HandleOp(level_, selection_, LevelPanel::Op::kLevelEdit).ok());
  EXPECT_FALSE(level_.has_value());
}

TEST_F(LevelPanelTest, HandleOp_Edit_FailsIfIndexOutOfBounds) {
  LevelPanelTestPeer::SetSelectedIndex(*panel_, 99);

  EXPECT_FALSE(panel_->HandleOp(level_, selection_, LevelPanel::Op::kLevelEdit).ok());
}

TEST_F(LevelPanelTest, HandleOp_Edit_LoadsLevelFromCache) {
  ON_CALL(*api_, GetAllLevels())
      .WillByDefault(Return(std::vector<Level>{{.id = "a-id", .name = "Alpha"}}));
  panel_->RefreshCache();
  LevelPanelTestPeer::SetSelectedIndex(*panel_, 0);

  ASSERT_TRUE(panel_->HandleOp(level_, selection_, LevelPanel::Op::kLevelEdit).ok());

  ASSERT_TRUE(level_.has_value());
  EXPECT_EQ(level_->id, "a-id");
  EXPECT_EQ(selection_.type, SelectionState::Type::kLevel);
}

// --- HandleOp::kLevelDelete ---

TEST_F(LevelPanelTest, HandleOp_Delete_FailsIfLevelLoaded) {
  level_ = Level{.id = "existing"};

  EXPECT_FALSE(panel_->HandleOp(level_, selection_, LevelPanel::Op::kLevelDelete).ok());
}

TEST_F(LevelPanelTest, HandleOp_Delete_NoopIfNothingSelected) {
  EXPECT_CALL(*api_, DeleteLevel(_)).Times(0);

  ASSERT_TRUE(panel_->HandleOp(level_, selection_, LevelPanel::Op::kLevelDelete).ok());
}

TEST_F(LevelPanelTest, HandleOp_Delete_CallsApiAndClearsSelection) {
  ON_CALL(*api_, GetAllLevels())
      .WillByDefault(Return(std::vector<Level>{{.id = "a-id", .name = "Alpha"}}));
  panel_->RefreshCache();
  LevelPanelTestPeer::SetSelectedIndex(*panel_, 0);
  selection_.type = SelectionState::Type::kLevel;

  EXPECT_CALL(*api_, DeleteLevel(StrEq("a-id"))).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*api_, GetAllLevels()).WillOnce(Return(std::vector<Level>{}));

  ASSERT_TRUE(panel_->HandleOp(level_, selection_, LevelPanel::Op::kLevelDelete).ok());
  EXPECT_EQ(selection_.type, SelectionState::Type::kNone);
}

// --- HandleOp::kLevelSave ---

TEST_F(LevelPanelTest, HandleOp_Save_FailsIfLevelNull) {
  EXPECT_FALSE(panel_->HandleOp(level_, selection_, LevelPanel::Op::kLevelSave).ok());
}

TEST_F(LevelPanelTest, HandleOp_Save_CallsUpdateAndRefreshes) {
  level_ = Level{.id = "a-id", .name = "Alpha"};

  EXPECT_CALL(*api_, UpdateLevel(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*api_, GetAllLevels()).WillOnce(Return(std::vector<Level>{*level_}));

  ASSERT_TRUE(panel_->HandleOp(level_, selection_, LevelPanel::Op::kLevelSave).ok());
}

// --- HandleOp::kLevelBack ---

TEST_F(LevelPanelTest, HandleOp_Back_ClearsLevelAndSelection) {
  level_ = Level{.id = "a-id"};
  selection_.type = SelectionState::Type::kLevel;

  ASSERT_TRUE(panel_->HandleOp(level_, selection_, LevelPanel::Op::kLevelBack).ok());

  EXPECT_FALSE(level_.has_value());
  EXPECT_EQ(selection_.type, SelectionState::Type::kNone);
}

// --- RefreshCache ---

TEST_F(LevelPanelTest, RefreshCache_SortsByName) {
  ON_CALL(*api_, GetAllLevels())
      .WillByDefault(Return(std::vector<Level>{
          {.id = "z-id", .name = "Zebra"},
          {.id = "a-id", .name = "Alpha"},
      }));

  panel_->RefreshCache();

  const std::vector<Level>& cache = LevelPanelTestPeer::GetLevelCache(*panel_);
  ASSERT_EQ(cache.size(), 2);
  EXPECT_EQ(cache[0].name, "Alpha");
  EXPECT_EQ(cache[1].name, "Zebra");
}

// --- RenderList ---

TEST_F(LevelPanelTest, RenderList_ShowsLevelNamesAsSelectables) {
  ON_CALL(*api_, GetAllLevels())
      .WillByDefault(Return(std::vector<Level>{
          {.id = "a-id", .name = "Alpha"},
          {.id = "z-id", .name = "Zebra"},
      }));
  panel_->RefreshCache();

  EXPECT_CALL(gui_, Selectable(StrEq("Alpha"), false, _, _)).WillOnce(Return(false));
  EXPECT_CALL(gui_, Selectable(StrEq("Zebra"), false, _, _)).WillOnce(Return(false));

  ASSERT_TRUE(panel_->RenderList(level_, selection_).ok());
}

// --- RenderDetails ---

TEST_F(LevelPanelTest, RenderDetails_SaveButton_TriggersSave) {
  level_ = Level{.id = "a-id", .name = "Alpha"};

  EXPECT_CALL(gui_, Button(StrEq("Save"), _)).WillOnce(Return(true));
  EXPECT_CALL(*api_, UpdateLevel(_)).WillOnce(Return(absl::OkStatus()));

  ASSERT_TRUE(panel_->RenderDetails(level_, selection_).ok());
}

}  // namespace
}  // namespace zebes
