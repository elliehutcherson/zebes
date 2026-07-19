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
    EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));
  }

  NiceMock<MockGui> gui_;
  LevelPanelModel model_;
  std::unique_ptr<LevelPanel> panel_;
};

TEST_F(LevelPanelTest, RenderListShowsOrderedLevelNames) {
  model_.SetLevels(
      {{.id = "z-id", .name = "Zebra"}, {.id = "a-id", .name = "Alpha"}});

  EXPECT_CALL(gui_, Selectable(StrEq("Alpha"), false, _, _)).WillOnce(Return(false));
  EXPECT_CALL(gui_, Selectable(StrEq("Zebra"), false, _, _)).WillOnce(Return(false));

  absl::StatusOr<LevelPanelEvent> event = panel_->RenderList(model_);
  ASSERT_TRUE(event.ok());
  EXPECT_EQ(event->action, LevelPanelAction::kNone);
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

}  // namespace
}  // namespace zebes
