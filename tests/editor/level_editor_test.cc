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
    editor.editting_level_ = std::move(level);
  }

  static void SetSelection(LevelEditor& editor, SelectionState selection) {
    editor.selection_ = std::move(selection);
  }

  static bool HasEditingLevel(const LevelEditor& editor) {
    return editor.editting_level_.has_value();
  }

  static bool HasSaveError(const LevelEditor& editor) {
    return editor.save_error_.has_value();
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

    ON_CALL(*mock_level_panel_, RenderList(_, _)).WillByDefault(Return(absl::OkStatus()));
    ON_CALL(*mock_level_panel_, RenderDetails(_, _)).WillByDefault(Return(absl::OkStatus()));

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
  EXPECT_CALL(*mock_level_panel_, RenderList(_, _)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*mock_level_panel_, RefreshCache()).Times(0);

  ASSERT_TRUE(LevelEditorTestPeer::RenderNavigator(*editor_).ok());
}

// --- RenderNavigator: level loaded, save ---

TEST_F(LevelEditorTest, RenderNavigator_SaveLevel_Success_CallsUpdateAndRefreshes) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id", .name = "Alpha"});

  EXPECT_CALL(gui_, Button(StrEq("Save Level"), _)).WillOnce(Return(true));
  EXPECT_CALL(*api_, UpdateLevel(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*mock_level_panel_, RefreshCache()).Times(1);

  ASSERT_TRUE(LevelEditorTestPeer::RenderNavigator(*editor_).ok());
  EXPECT_FALSE(LevelEditorTestPeer::HasSaveError(*editor_));
}

TEST_F(LevelEditorTest, RenderNavigator_SaveLevel_Failure_DoesNotCallRefresh) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id", .name = "Alpha"});

  EXPECT_CALL(gui_, Button(StrEq("Save Level"), _)).WillOnce(Return(true));
  EXPECT_CALL(*api_, UpdateLevel(_)).WillOnce(Return(absl::InternalError("disk full")));
  EXPECT_CALL(*mock_level_panel_, RefreshCache()).Times(0);

  ASSERT_TRUE(LevelEditorTestPeer::RenderNavigator(*editor_).ok());
  EXPECT_TRUE(LevelEditorTestPeer::HasSaveError(*editor_));
}

// --- RenderNavigator: close level ---

TEST_F(LevelEditorTest, RenderNavigator_CloseLevel_DelegatesListOnNextRender) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id", .name = "Alpha"});

  // First render: Close Level pressed → level cleared.
  EXPECT_CALL(gui_, Button(StrEq("Close Level"), _)).WillOnce(Return(true));
  ASSERT_TRUE(LevelEditorTestPeer::RenderNavigator(*editor_).ok());

  // Second render: now no level is loaded → delegates to level_panel_->RenderList().
  EXPECT_CALL(*mock_level_panel_, RenderList(_, _)).WillOnce(Return(absl::OkStatus()));
  ASSERT_TRUE(LevelEditorTestPeer::RenderNavigator(*editor_).ok());
}

// --- RenderInspector ---

TEST_F(LevelEditorTest, RenderInspector_NoLevel_DoesNotDelegateToLevelPanel) {
  // editting_level_ is nullopt by default.
  EXPECT_CALL(*mock_level_panel_, RenderDetails(_, _)).Times(0);

  ASSERT_TRUE(LevelEditorTestPeer::RenderInspector(*editor_).ok());
}

TEST_F(LevelEditorTest, RenderInspector_kLevelSelection_DelegatesToLevelPanel) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id"});
  SelectionState selection;
  selection.type = SelectionState::Type::kLevel;
  LevelEditorTestPeer::SetSelection(*editor_, selection);

  EXPECT_CALL(*mock_level_panel_, RenderDetails(_, _)).WillOnce(Return(absl::OkStatus()));

  ASSERT_TRUE(LevelEditorTestPeer::RenderInspector(*editor_).ok());
}

TEST_F(LevelEditorTest, RenderInspector_kNoneSelection_DoesNotDelegateToLevelPanel) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id"});
  // selection_.type == kNone by default.

  EXPECT_CALL(*mock_level_panel_, RenderDetails(_, _)).Times(0);

  ASSERT_TRUE(LevelEditorTestPeer::RenderInspector(*editor_).ok());
}

}  // namespace
}  // namespace zebes
