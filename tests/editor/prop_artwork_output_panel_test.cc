#include "editor/prop_artwork_editor/prop_artwork_output_panel.h"

#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

class PropArtworkOutputPanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(panel_, PropArtworkOutputPanel::Create(&gui_));
    ON_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillByDefault(Invoke([this](const char* label, const char* preview, ImGuiComboFlags) {
          return ScopedCombo(&gui_, label, preview);
        }));
    ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(false));
    ON_CALL(gui_, Button(_, _)).WillByDefault(Return(false));
    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(Invoke([this](ImGuiCol index, const ImVec4& color) {
          return ScopedStyleColor(&gui_, index, color);
        }));
  }

  void ClickOnly(const std::string& label) {
    ON_CALL(gui_, Button(_, _)).WillByDefault(Invoke([label](const char* candidate, const ImVec2&) {
      return label == candidate;
    }));
  }

  NiceMock<MockGui> gui_;
  std::unique_ptr<PropArtworkOutputPanel> panel_;
  PropArtworkEditorModel model_;
};

TEST_F(PropArtworkOutputPanelTest, ClearingTheWorkspaceRequiresConfirmation) {
  ClickOnly("Clear workspace##PropArtworkClear");
  EXPECT_EQ(panel_->Render(model_, {}, false), PropArtworkOutputPanel::Action::kNone);

  ClickOnly("Confirm##PropArtworkClear");
  EXPECT_EQ(panel_->Render(model_, {}, false), PropArtworkOutputPanel::Action::kClearWorkspace);
}

}  // namespace
}  // namespace zebes
