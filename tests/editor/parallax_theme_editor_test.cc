#include "editor/parallax_theme_editor/parallax_theme_editor.h"

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"
#include "tests/macros.h"

namespace zebes {

class ParallaxThemeEditorTestPeer {
 public:
  static ParallaxThemeEditorModel& Model(ParallaxThemeEditor& editor) { return editor.model_; }

  static absl::Status RenderToolbar(ParallaxThemeEditor& editor) {
    bool save_requested = false;
    return editor.RenderToolbar(*editor.model_.draft(), save_requested);
  }
};

namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

class ParallaxThemeEditorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(editor_, ParallaxThemeEditor::Create(&api_, &gui_));
    ParallaxThemeEditorTestPeer::Model(*editor_).Open({
        .id = "theme-1",
        .name = "Cave Theme",
        .layers = {{.name = "Far"}},
    });
    ON_CALL(gui_, Button(_, _)).WillByDefault(Return(false));
    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(Invoke([this](ImGuiCol index, const ImVec4& color) {
          return ScopedStyleColor(&gui_, index, color);
        }));
  }

  ParallaxThemeEditorModel& model() { return ParallaxThemeEditorTestPeer::Model(*editor_); }

  NiceMock<MockApi> api_;
  NiceMock<MockGui> gui_;
  std::unique_ptr<ParallaxThemeEditor> editor_;
};

TEST_F(ParallaxThemeEditorTest, BackToThemeListClosesACleanTheme) {
  EXPECT_CALL(gui_, Button(StrEq("Back"), _)).WillOnce(Return(true));

  ASSERT_OK(ParallaxThemeEditorTestPeer::RenderToolbar(*editor_));

  EXPECT_FALSE(model().has_draft());
}

TEST_F(ParallaxThemeEditorTest, BackToThemeListConfirmsBeforeDiscardingChanges) {
  model().draft()->name = "Changed Cave Theme";
  EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(gui_, Button(StrEq("Back"), _)).WillOnce(Return(true));

  ASSERT_OK(ParallaxThemeEditorTestPeer::RenderToolbar(*editor_));
  ASSERT_TRUE(model().has_draft());

  EXPECT_CALL(gui_, Button(StrEq("Confirm##ParallaxThemeBack"), _)).WillOnce(Return(true));
  ASSERT_OK(ParallaxThemeEditorTestPeer::RenderToolbar(*editor_));

  EXPECT_FALSE(model().has_draft());
}

}  // namespace
}  // namespace zebes
