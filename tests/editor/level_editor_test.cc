#include "editor/level_editor/level_editor.h"

#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/level.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"
#include "tests/editor/mock_level_panel.h"

namespace zebes {

class LevelEditorTestPeer {
 public:
  static absl::Status RenderNavigator(LevelEditor& editor) {
    return editor.RenderNavigator();
  }

  static absl::Status RenderInspector(LevelEditor& editor) {
    return editor.RenderInspector();
  }

  static void SetEditingLevel(LevelEditor& editor, std::optional<Level> level) {
    if (level.has_value()) {
      editor.level_model_.BeginEditingLevel(std::move(*level));
    } else {
      editor.level_model_.CloseActiveLevel();
    }
  }

  static void SetSelection(LevelEditor& editor, SelectionState selection) {
    editor.selection_ = std::move(selection);
  }

  static bool HasEditingLevel(const LevelEditor& editor) {
    return editor.level_model_.has_active_level();
  }

  static bool HasSaveError(const LevelEditor& editor) {
    return editor.save_error_.has_value();
  }

  static LevelPanelModel& GetLevelModel(LevelEditor& editor) { return editor.level_model_; }

  static SelectionState::Type GetSelectionType(const LevelEditor& editor) {
    return editor.selection_.type;
  }

  static absl::Status HandleLevelPanelEvent(LevelEditor& editor, LevelPanelEvent event) {
    return editor.HandleLevelPanelEvent(event);
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

// Standalone Create validation tests.
TEST(LevelEditorCreateTest, FailsWithNullApi) {
  NiceMock<MockGui> gui;
  auto result = LevelEditor::Create({.api = nullptr, .gui = &gui});
  EXPECT_FALSE(result.ok());
}

TEST(LevelEditorCreateTest, FailsWithNullGui) {
  NiceMock<MockApi> api;
  auto result = LevelEditor::Create({.api = &api, .gui = nullptr});
  EXPECT_FALSE(result.ok());
}

TEST(SelectionStateTest, EmptyViewportPickPreservesZoneSelection) {
  SelectionState selection{.type = SelectionState::Type::kZone, .zone_id = 7};

  selection.ApplyEntityPick(Entity::kInvalidId);

  EXPECT_EQ(selection.type, SelectionState::Type::kZone);
  EXPECT_EQ(selection.zone_id, 7);
}

TEST(SelectionStateTest, EmptyViewportPickClearsEntitySelection) {
  SelectionState selection{.type = SelectionState::Type::kEntity, .entity_id = 42};

  selection.ApplyEntityPick(Entity::kInvalidId);

  EXPECT_EQ(selection.type, SelectionState::Type::kNone);
  EXPECT_EQ(selection.entity_id, Entity::kInvalidId);
}

TEST(SelectionStateTest, EntityPickReplacesOtherSelection) {
  SelectionState selection{.type = SelectionState::Type::kZone, .zone_id = 7};

  selection.ApplyEntityPick(42);

  EXPECT_EQ(selection.type, SelectionState::Type::kEntity);
  EXPECT_EQ(selection.entity_id, 42);
  EXPECT_EQ(selection.zone_id, -1);
}

class LevelEditorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Set up API defaults needed for concrete sub-panel construction.
    // ParallaxThemePanel calls GetAllTextures() during Create().
    ON_CALL(*api_, GetAllTextures()).WillByDefault(Return(std::vector<Texture>{}));

    // GUI defaults.
    static ImGuiIO io;
    ON_CALL(gui_, GetIO()).WillByDefault(ReturnRef(io));
    static ImGuiStyle style;
    ON_CALL(gui_, GetStyle()).WillByDefault(ReturnRef(style));
    ON_CALL(gui_, GetContentRegionAvail()).WillByDefault(Return(ImVec2(800, 600)));

    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(
            [&](ImGuiCol idx, const ImVec4& col) { return ScopedStyleColor(&gui_, {}, {}); });
    ON_CALL(gui_, CreateScopedId(::testing::A<const char*>()))
        .WillByDefault(Invoke([this](const char* id) { return ScopedId(&gui_, id); }));

    // All buttons return false by default; individual tests override as needed.
    EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));

    // Retain a raw pointer to the mock level panel for EXPECT_CALL after the
    // unique_ptr is moved into LevelEditor::Options.
    auto mock_panel = std::make_unique<NiceMock<MockLevelPanel>>();
    mock_level_panel_ = mock_panel.get();

    ON_CALL(*mock_level_panel_, RenderList(_)).WillByDefault(Return(LevelPanelEvent{}));
    ON_CALL(*mock_level_panel_, RenderDetails(_)).WillByDefault(Return(LevelPanelEvent{}));

    auto editor_or = LevelEditor::Create({
        .api = api_.get(),
        .gui = &gui_,
        .level_panel = std::move(mock_panel),
    });
    ASSERT_TRUE(editor_or.ok());
    editor_ = *std::move(editor_or);
  }

  std::unique_ptr<MockApi> api_ = std::make_unique<NiceMock<MockApi>>();
  NiceMock<MockGui> gui_;
  std::unique_ptr<LevelEditor> editor_;
  MockLevelPanel* mock_level_panel_ = nullptr;
};

// --- RenderNavigator: no level loaded ---

TEST_F(LevelEditorTest, RenderNavigator_NoLevel_DelegatesToLevelPanel) {
  EXPECT_CALL(*mock_level_panel_, RenderList(_)).WillOnce(Return(LevelPanelEvent{}));

  ASSERT_TRUE(LevelEditorTestPeer::RenderNavigator(*editor_).ok());
}

// --- RenderNavigator: level loaded, save ---

TEST_F(LevelEditorTest, RenderNavigator_SaveLevel_Success_CallsUpdateAndRefreshes) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id", .name = "Alpha"});

  EXPECT_CALL(gui_, Button(StrEq("Save Level"), _)).WillOnce(Return(true));
  EXPECT_CALL(*api_, UpdateLevel(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*api_, GetAllLevels()).WillOnce(Return(std::vector<Level>{}));

  ASSERT_TRUE(LevelEditorTestPeer::RenderNavigator(*editor_).ok());
  EXPECT_FALSE(LevelEditorTestPeer::HasSaveError(*editor_));
}

TEST_F(LevelEditorTest, RenderNavigator_SaveLevel_Failure_DoesNotCallRefresh) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id", .name = "Alpha"});

  EXPECT_CALL(gui_, Button(StrEq("Save Level"), _)).WillOnce(Return(true));
  EXPECT_CALL(*api_, UpdateLevel(_)).WillOnce(Return(absl::InternalError("disk full")));
  EXPECT_CALL(*api_, GetAllLevels()).Times(0);

  ASSERT_TRUE(LevelEditorTestPeer::RenderNavigator(*editor_).ok());
  EXPECT_TRUE(LevelEditorTestPeer::HasSaveError(*editor_));
}

// --- RenderNavigator: close level ---

TEST_F(LevelEditorTest, RenderNavigator_CloseLevel_DelegatesListOnNextRender) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id", .name = "Alpha"});

  // First render: Close Level pressed → level cleared.
  EXPECT_CALL(gui_, Button(StrEq("Close Level"), _)).WillOnce(Return(true));
  ASSERT_TRUE(LevelEditorTestPeer::RenderNavigator(*editor_).ok());

  // Second render: now no level is loaded and delegates to the list panel.
  EXPECT_CALL(*mock_level_panel_, RenderList(_)).WillOnce(Return(LevelPanelEvent{}));
  ASSERT_TRUE(LevelEditorTestPeer::RenderNavigator(*editor_).ok());
}

// --- RenderInspector ---

TEST_F(LevelEditorTest, RenderInspector_NoLevel_DoesNotDelegateToLevelPanel) {
  EXPECT_CALL(*mock_level_panel_, RenderDetails(_)).Times(0);

  ASSERT_TRUE(LevelEditorTestPeer::RenderInspector(*editor_).ok());
}

TEST_F(LevelEditorTest, RenderInspector_kLevelSelection_DelegatesToLevelPanel) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id"});
  SelectionState selection;
  selection.type = SelectionState::Type::kLevel;
  LevelEditorTestPeer::SetSelection(*editor_, selection);

  EXPECT_CALL(*mock_level_panel_, RenderDetails(_)).WillOnce(Return(LevelPanelEvent{}));

  ASSERT_TRUE(LevelEditorTestPeer::RenderInspector(*editor_).ok());
}

TEST_F(LevelEditorTest, RenderInspector_kNoneSelection_DoesNotDelegateToLevelPanel) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id"});
  // selection_.type == kNone by default.

  EXPECT_CALL(*mock_level_panel_, RenderDetails(_)).Times(0);

  ASSERT_TRUE(LevelEditorTestPeer::RenderInspector(*editor_).ok());
}

TEST_F(LevelEditorTest, CreateEventPersistsDraftAndSelectsLevel) {
  LevelPanelModel& model = LevelEditorTestPeer::GetLevelModel(*editor_);
  model.BeginNewLevel();
  EXPECT_CALL(*api_, CreateLevel(_)).WillOnce(Return(std::string("new-id")));
  EXPECT_CALL(*api_, GetAllLevels()).WillOnce(Return(std::vector<Level>{}));

  ASSERT_TRUE(LevelEditorTestPeer::HandleLevelPanelEvent(
                  *editor_, LevelPanelEvent{.action = LevelPanelAction::kCreate})
                  .ok());

  ASSERT_NE(model.active_level(), nullptr);
  EXPECT_EQ(model.active_level()->id, "new-id");
  EXPECT_EQ(LevelEditorTestPeer::GetSelectionType(*editor_), SelectionState::Type::kLevel);
}

TEST_F(LevelEditorTest, FailedDeletePreservesModelSelection) {
  LevelPanelModel& model = LevelEditorTestPeer::GetLevelModel(*editor_);
  model.SetLevels({{.id = "cave", .name = "Cave"}});
  ASSERT_TRUE(model.SelectLevel("cave").ok());
  EXPECT_CALL(*api_, DeleteLevel(StrEq("cave")))
      .WillOnce(Return(absl::InternalError("disk full")));
  EXPECT_CALL(*api_, GetAllLevels()).Times(0);

  EXPECT_FALSE(LevelEditorTestPeer::HandleLevelPanelEvent(
                   *editor_, LevelPanelEvent{.action = LevelPanelAction::kDelete})
                   .ok());

  EXPECT_EQ(model.selected_level_id(), "cave");
}

}  // namespace
}  // namespace zebes
