#include "editor/confirm_prompt.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

class ConfirmPromptTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ON_CALL(gui_, CreateScopedStyleColor(_, ::testing::An<const ImVec4&>()))
        .WillByDefault(Invoke([this](ImGuiCol idx, const ImVec4& col) {
          return ScopedStyleColor(&gui_, idx, col);
        }));
    ON_CALL(gui_, Button(_, _)).WillByDefault(Return(false));
  }

  // Makes exactly one button report a click, so a render exercises one answer.
  void ClickOnly(std::string label) {
    ON_CALL(gui_, Button(_, _))
        .WillByDefault(
            Invoke([label](const char* pressed, const ImVec2&) { return label == pressed; }));
  }

  void ClickNothing() { ON_CALL(gui_, Button(_, _)).WillByDefault(Return(false)); }

  // Every label the prompt drew a button for during one render.
  std::vector<std::string> RenderCapturingLabels(const std::string& target) {
    std::vector<std::string> labels;
    ON_CALL(gui_, Button(_, _)).WillByDefault(Invoke([&labels](const char* label, const ImVec2&) {
      labels.push_back(label);
      return false;
    }));
    prompt_.Render(gui_, "Delete", target, "Delete it?", "Thing");
    return labels;
  }

  bool Render(const std::string& target) {
    return prompt_.Render(gui_, "Delete", target, "Delete it?", "Thing");
  }

  NiceMock<MockGui> gui_;
  ConfirmPrompt prompt_;
};

TEST_F(ConfirmPromptTest, TheFirstClickAsksInsteadOfActing) {
  ClickOnly("Delete##Thing");
  EXPECT_FALSE(Render("grass"));
  EXPECT_TRUE(prompt_.armed());

  // The question replaces the button rather than sitting beside it, so there is
  // no way to press Delete twice and have it mean something different.
  const std::vector<std::string> labels = RenderCapturingLabels("grass");
  EXPECT_THAT(labels, ::testing::ElementsAre("Confirm##Thing", "Cancel##Thing"));
}

TEST_F(ConfirmPromptTest, ConfirmingActsExactlyOnce) {
  ClickOnly("Delete##Thing");
  Render("grass");

  ClickOnly("Confirm##Thing");
  EXPECT_TRUE(Render("grass"));

  // Still holding the button down on the next frame must not act again.
  EXPECT_FALSE(Render("grass"));
  EXPECT_FALSE(prompt_.armed());
}

TEST_F(ConfirmPromptTest, CancellingRestoresThePlainButton) {
  ClickOnly("Delete##Thing");
  Render("grass");

  ClickOnly("Cancel##Thing");
  EXPECT_FALSE(Render("grass"));
  EXPECT_FALSE(prompt_.armed());

  ClickNothing();
  EXPECT_THAT(RenderCapturingLabels("grass"), ::testing::ElementsAre("Delete##Thing"));
}

// A question belongs to the thing it was raised against. Without this, moving
// the selection while a Confirm is on screen would leave it primed to destroy
// whatever is selected now.
TEST_F(ConfirmPromptTest, ARenderForADifferentTargetDropsTheQuestion) {
  ClickOnly("Delete##Thing");
  Render("grass");
  ASSERT_TRUE(prompt_.armed());

  ClickOnly("Confirm##Thing");
  EXPECT_FALSE(Render("stone")) << "a primed Confirm acted on the new selection";
  EXPECT_FALSE(prompt_.armed());
}

TEST_F(ConfirmPromptTest, DisarmDropsTheQuestion) {
  ClickOnly("Delete##Thing");
  Render("grass");
  ASSERT_TRUE(prompt_.armed());

  prompt_.Disarm();

  EXPECT_FALSE(prompt_.armed());
  ClickOnly("Confirm##Thing");
  EXPECT_FALSE(Render("grass"));
}

// A per-row button has no space for a sentence above it, and the row already
// says which item it belongs to. The compact form still has to offer both
// answers.
TEST_F(ConfirmPromptTest, AnEmptyQuestionStillOffersBothAnswers) {
  ClickOnly("Delete##Row");
  ASSERT_FALSE(prompt_.Render(gui_, "Delete", "grass", "", "Row"));
  ASSERT_TRUE(prompt_.armed());

  std::vector<std::string> labels;
  ON_CALL(gui_, Button(_, _)).WillByDefault(Invoke([&labels](const char* label, const ImVec2&) {
    labels.push_back(label);
    return false;
  }));
  prompt_.Render(gui_, "Delete", "grass", "", "Row");

  EXPECT_THAT(labels, ::testing::ElementsAre("Confirm##Row", "Cancel##Row"));
}

// Some actions are only destructive sometimes -- closing an editor matters
// when there are unsaved edits and not otherwise -- so the caller owns the
// button and arms the prompt itself.
TEST_F(ConfirmPromptTest, ACallerCanRaiseTheQuestionItself) {
  prompt_.Arm("grass");
  EXPECT_TRUE(prompt_.armed());

  ClickNothing();
  EXPECT_THAT(RenderCapturingLabels("grass"),
              ::testing::ElementsAre("Confirm##Thing", "Cancel##Thing"));

  ClickOnly("Confirm##Thing");
  EXPECT_TRUE(Render("grass"));
}

// Two prompts in one panel must not share ImGui ids, or clicking one answers
// the other.
TEST_F(ConfirmPromptTest, TheIdSuffixSeparatesPromptsInOnePanel) {
  ConfirmPrompt other;
  ClickOnly("Delete##Second");

  EXPECT_FALSE(prompt_.Render(gui_, "Delete", "grass", "Delete it?", "First"));
  EXPECT_FALSE(prompt_.armed());

  EXPECT_FALSE(other.Render(gui_, "Delete", "stone", "Delete it?", "Second"));
  EXPECT_TRUE(other.armed());
}

}  // namespace
}  // namespace zebes
