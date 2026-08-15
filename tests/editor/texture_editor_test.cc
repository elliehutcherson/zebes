#include "editor/texture_editor/texture_editor.h"

#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/texture.h"
#include "tests/api_mock.h"
#include "tests/common/mock_sdl_wrapper.h"
#include "tests/editor/mock_gui.h"

namespace zebes {

// Reaches the details column, which is where deletion lives. Rendering the tab
// whole would drag in the table, the list box and the SDL preview for a button
// that depends on none of them.
class TextureEditorTestPeer {
 public:
  static void RenderTextureDetails(TextureEditor& editor) { editor.RenderTextureDetails(); }
  static TextureEditorModel& GetModel(TextureEditor& editor) { return editor.model_; }
};

namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Contains;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::Return;
using ::testing::StrEq;

Texture MakeTexture(std::string id, std::string name) {
  return Texture{.id = std::move(id), .name = std::move(name), .path = "assets/textures/x.png"};
}

class TextureEditorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(Invoke(
            [this](ImGuiCol idx, const ImVec4& col) { return ScopedStyleColor(&gui_, idx, col); }));
    ON_CALL(api_, GetAllTextures()).WillByDefault(Invoke([this] {
      return absl::StatusOr<std::vector<Texture>>(textures_);
    }));

    absl::StatusOr<std::unique_ptr<TextureEditor>> editor =
        TextureEditor::Create(&api_, &sdl_, &gui_);
    ASSERT_OK(editor);
    editor_ = *std::move(editor);
  }

  TextureEditorModel& model() { return TextureEditorTestPeer::GetModel(*editor_); }
  void RenderDetails() { TextureEditorTestPeer::RenderTextureDetails(*editor_); }

  // Every button label the details column drew during one render.
  std::vector<std::string> RenderCapturingLabels() {
    std::vector<std::string> labels;
    ON_CALL(gui_, Button(_, _)).WillByDefault(Invoke([&labels](const char* label, const ImVec2&) {
      labels.push_back(label);
      return false;
    }));
    RenderDetails();
    ON_CALL(gui_, Button(_, _)).WillByDefault(Return(false));
    return labels;
  }

  // Makes exactly one button report a click, so a render exercises one answer.
  void ClickOnly(std::string label) {
    ON_CALL(gui_, Button(_, _)).WillByDefault(Invoke([label](const char* pressed, const ImVec2&) {
      return label == pressed;
    }));
  }

  void ClickNothing() { ON_CALL(gui_, Button(_, _)).WillByDefault(Return(false)); }

  NiceMock<MockApi> api_;
  NiceMock<MockSdlWrapper> sdl_;
  NiceMock<MockGui> gui_;
  std::unique_ptr<TextureEditor> editor_;
  std::vector<Texture> textures_{MakeTexture("tex-1", "cave_wall")};
};

TEST_F(TextureEditorTest, OffersNoDeleteWithoutASelection) {
  EXPECT_THAT(RenderCapturingLabels(), Not(Contains("Delete##Texture")));
}

// A texture being created has nothing on disk yet, so there is nothing for a
// delete to remove -- New Texture and picking a row already abandon it.
TEST_F(TextureEditorTest, OffersNoDeleteWhileCreating) {
  model().BeginNewTexture();

  EXPECT_THAT(RenderCapturingLabels(), Not(Contains("Delete##Texture")));
}

TEST_F(TextureEditorTest, OffersDeleteForAnImportedTexture) {
  model().SelectTexture(textures_[0]);

  EXPECT_THAT(RenderCapturingLabels(), Contains("Delete##Texture"));
}

TEST_F(TextureEditorTest, TheFirstClickAsksInsteadOfDeleting) {
  model().SelectTexture(textures_[0]);
  EXPECT_CALL(api_, DeleteTexture(_)).Times(0);

  ClickOnly("Delete##Texture");
  RenderDetails();
}

TEST_F(TextureEditorTest, TheQuestionNamesTheTextureAndSaysTheImageGoesToo) {
  model().SelectTexture(textures_[0]);
  ClickOnly("Delete##Texture");
  RenderDetails();

  ClickNothing();
  gui_.ClearWrappedText();
  RenderDetails();

  EXPECT_THAT(gui_.wrapped_text(),
              Contains("Delete 'cave_wall'? Its definition and its image file both go."));
}

TEST_F(TextureEditorTest, ConfirmingDeletesTheSelectedTexture) {
  model().SelectTexture(textures_[0]);
  ClickOnly("Delete##Texture");
  RenderDetails();

  EXPECT_CALL(api_, DeleteTexture(StrEq("tex-1"))).WillOnce(Return(absl::OkStatus()));
  ClickOnly("Confirm##Texture");
  RenderDetails();

  // The tab cannot go on showing a texture that is gone, and the list it was
  // picked from is stale until it is read again.
  EXPECT_FALSE(model().has_selection());
}

// A refusal names every referrer, which is what the user has to go and change.
// Losing it would leave a delete that silently did nothing.
TEST_F(TextureEditorTest, ARefusedDeleteKeepsTheSelectionAndReportsWhy) {
  model().SelectTexture(textures_[0]);
  ClickOnly("Delete##Texture");
  RenderDetails();

  EXPECT_CALL(api_, DeleteTexture(StrEq("tex-1")))
      .WillOnce(Return(absl::FailedPreconditionError(
          "Cannot delete texture 'cave_wall'. 1 thing references it:\n  Tileset 'Cave' "
          "(texture_id)")));
  ClickOnly("Confirm##Texture");
  RenderDetails();

  ASSERT_TRUE(model().error().has_value());
  EXPECT_THAT(*model().error(), ::testing::HasSubstr("Tileset 'Cave' (texture_id)"));
  EXPECT_TRUE(model().has_selection()) << "the user still needs the texture the refusal is about";
}

// The question belongs to the texture it was raised against. Answering it after
// the selection moved would delete whatever is showing now.
TEST_F(TextureEditorTest, SelectingADifferentTextureDropsAPendingQuestion) {
  textures_.push_back(MakeTexture("tex-2", "sky"));
  model().SelectTexture(textures_[0]);
  ClickOnly("Delete##Texture");
  RenderDetails();

  model().SelectTexture(textures_[1]);
  EXPECT_CALL(api_, DeleteTexture(_)).Times(0);
  ClickOnly("Confirm##Texture");
  RenderDetails();
}

}  // namespace
}  // namespace zebes
