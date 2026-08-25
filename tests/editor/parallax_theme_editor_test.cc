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
  static absl::Status OpenTheme(ParallaxThemeEditor& editor, const std::string& id) {
    return editor.OpenTheme(id);
  }
  static CameraCenterRoute ManualRoute(const ParallaxThemeEditor& editor) {
    return {.min = editor.manual_route_min_, .max = editor.manual_route_max_};
  }
  static bool EditMode(const ParallaxThemeEditor& editor) { return editor.edit_mode_; }
  static void SetEditMode(ParallaxThemeEditor& editor, bool enabled) {
    editor.edit_mode_ = enabled;
  }
  static absl::Status RenderTexturePicker(ParallaxThemeEditor& editor, int layer_index,
                                          ParallaxElement& element,
                                          const std::vector<Texture>& textures) {
    return editor.RenderTexturePicker(layer_index, element, textures);
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
    ON_CALL(gui_, CreateScopedChild(_, _, _, _))
        .WillByDefault(
            Invoke([this](const char* id, ImVec2 size, bool border, ImGuiWindowFlags flags) {
              return ScopedChild(&gui_, id, size, border, flags);
            }));
    ON_CALL(gui_, BeginChild(_, _, _, _)).WillByDefault(Return(true));
    ON_CALL(gui_, CreateScopedId(An<const char*>())).WillByDefault(Invoke([this](const char* id) {
      return ScopedId(&gui_, id);
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

TEST_F(ParallaxThemeEditorTest, ReopeningUnassignedThemeFitsRouteToSavedContent) {
  ParallaxTheme saved{
      .id = "wide-theme",
      .name = "Wide Theme",
      .layers = {{
          .name = "Near",
          .scroll_factor = {0.5, 0.25},
          .elements =
              {
                  {.id = 0, .name = "Start", .position = {0, 0}, .texture_id = "start"},
                  {.id = 1, .name = "Gate", .position = {6000, 0}, .texture_id = "gate"},
              },
      }},
  };
  ON_CALL(api_, GetParallaxTheme(StrEq(saved.id))).WillByDefault(Return(&saved));
  ON_CALL(api_, GetAllLevels()).WillByDefault(Return(std::vector<Level>{}));

  ASSERT_OK(ParallaxThemeEditorTestPeer::OpenTheme(*editor_, saved.id));

  const CameraCenterRoute route = ParallaxThemeEditorTestPeer::ManualRoute(*editor_);
  EXPECT_EQ(route.min, Vec(320, 180));
  EXPECT_EQ(route.max, Vec(12320, 180));
  EXPECT_FALSE(ParallaxThemeEditorTestPeer::EditMode(*editor_));
}

TEST_F(ParallaxThemeEditorTest, OpenedThemeRequiresAnExplicitSwitchToEditMode) {
  EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(gui_, Button(StrEq("Edit Theme"), _)).WillOnce(Return(true));

  ASSERT_OK(ParallaxThemeEditorTestPeer::RenderToolbar(*editor_));

  EXPECT_TRUE(ParallaxThemeEditorTestPeer::EditMode(*editor_));
}

TEST_F(ParallaxThemeEditorTest, DiscardChangesRestoresTheSavedThemeWithoutClosingIt) {
  ParallaxThemeEditorTestPeer::SetEditMode(*editor_, true);
  model().draft()->name = "Accidental rename";
  EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(gui_, Button(StrEq("Discard Changes##ParallaxThemeDiscard"), _))
      .WillOnce(Return(true));

  ASSERT_OK(ParallaxThemeEditorTestPeer::RenderToolbar(*editor_));
  EXPECT_TRUE(model().dirty());

  EXPECT_CALL(gui_, Button(StrEq("Confirm##ParallaxThemeDiscard"), _)).WillOnce(Return(true));
  ASSERT_OK(ParallaxThemeEditorTestPeer::RenderToolbar(*editor_));

  ASSERT_TRUE(model().has_draft());
  EXPECT_EQ(model().draft()->name, "Cave Theme");
  EXPECT_FALSE(model().dirty());
  EXPECT_FALSE(ParallaxThemeEditorTestPeer::EditMode(*editor_));
}

TEST_F(ParallaxThemeEditorTest, TextureChoiceRequiresExplicitApply) {
  ParallaxElement element{.id = 0, .name = "Formation", .texture_id = "saved-texture"};
  const std::vector<Texture> textures = {
      {.id = "saved-texture", .name = "Saved"},
      {.id = "candidate-texture", .name = "Candidate"},
  };
  ON_CALL(api_, GetTextureHandle(_)).WillByDefault(Return(TextureHandle{}));
  EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(gui_, Selectable(An<const char*>(), An<bool>(), _, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(gui_, Selectable(StrEq(textures[1].name_id()), false, _, _)).WillOnce(Return(true));

  ASSERT_OK(ParallaxThemeEditorTestPeer::RenderTexturePicker(*editor_, 0, element, textures));
  EXPECT_EQ(element.texture_id, "saved-texture");

  EXPECT_CALL(gui_, Button(StrEq("Apply Texture"), _)).WillOnce(Return(true));
  ASSERT_OK(ParallaxThemeEditorTestPeer::RenderTexturePicker(*editor_, 0, element, textures));
  EXPECT_EQ(element.texture_id, "candidate-texture");
}

}  // namespace
}  // namespace zebes
