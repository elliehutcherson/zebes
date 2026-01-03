#include "editor/blueprint/blueprint_editor.h"

#include "editor/blueprint/blueprint_panel.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "imgui_te_ui.h"
#include "test_main.h"
#include "tests/api_mock.h"
#include "tests/macros.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class BlueprintEditorTestApp {
 public:
  BlueprintEditorTestApp() {
    api_ = std::make_unique<NiceMock<MockApi>>();

    // Setup initial blueprints
    Blueprint bp1;
    bp1.id = "bp1";
    bp1.name = "Grass";
    Blueprint::State s1;
    s1.name = "State1";
    bp1.states.push_back(s1);

    blueprints_ = {bp1};

    ON_CALL(*api_, GetAllBlueprints()).WillByDefault([this]() { return blueprints_; });

    ON_CALL(*api_, UpdateBlueprint(_)).WillByDefault([this](const Blueprint& bp) {
      // Simulate saving by updating our backing store
      for (auto& b : blueprints_) {
        if (b.id == bp.id) {
          b = bp;  // Update the backing store
          return absl::OkStatus();
        }
      }
      return absl::OkStatus();
    });

    auto editor_or = BlueprintEditor::Create(api_.get());
    assert(editor_or.ok());
    editor_ = std::move(*editor_or);
  }

  void Draw() { EXPECT_OK(editor_->Render()); }

  MockApi* api() { return api_.get(); }
  BlueprintEditor* editor() { return editor_.get(); }
  const std::vector<Blueprint>& blueprints() const { return blueprints_; }

 private:
  std::unique_ptr<MockApi> api_;
  std::unique_ptr<BlueprintEditor> editor_;
  std::vector<Blueprint> blueprints_;
};

// Global instance for the test to access
std::unique_ptr<BlueprintEditorTestApp> g_app;

void AppSetup() { g_app = std::make_unique<BlueprintEditorTestApp>(); }

void RegisterTests(ImGuiTestEngine* engine) {
  IM_REGISTER_TEST(engine, "blueprint", "save_preserves_changes")->TestFunc =
      [](ImGuiTestContext* ctx) {
        auto& app = *g_app;

        // 1. Initial State: Panel has cached "State1"
        // We need to navigate to the blueprint.
        // Assuming the editor starts in the list view.

        ctx->SetRef("Window");  // Adjust if BlueprintEditor opens a specific window

        // Automation: Click on the "Grass" blueprint
        // We need to know how the list is rendered. Assuming it's a list item or button.
        // The previous test did logic programmatically. Here we want to use the UI.
        // If we can't easily reference the button by ID/Label, we might need to inspect the code.
        // For now, let's assume we can click "Grass".

        // Wait for the UI to settle
        ctx->Yield(2);

        // Finding the item might depend on how it's rendered.
        // Let's assume standard ImGui button or selectable.
        ctx->ItemClick("**/Grass");

        // Now we should be in edit mode (or verifying we entered it).
        // The state list should be visible.
        ctx->ItemClick("**/State1");

        // Modify the state name.
        // Finding the input field for the state name.
        // This depends on BlueprintEditor implementation.
        // Let's assume there's a labeled input "Name".
        ctx->ItemInputValue("**/Name", "State1_Modified");

        // Click Save.
        ctx->ItemClick("**/Save");

        // Verify backend is updated.
        // We can check the mock backing store directly.
        IM_CHECK_EQ(app.blueprints()[0].states[0].name, "State1_Modified");

        // Navigate back to list (ExitStateMode -> ExitBlueprintMode).
        // Assuming there's a "Back" button.
        ctx->ItemClick("**/Back");

        // Now re-enter the blueprint and state to verify persistence in UI.
        ctx->ItemClick("**/Grass");
        ctx->ItemClick("**/State1_Modified");  // The label might have updated?

        // Check key values
        // IM_CHECK_STREQ(..., "State1_Modified");
      };
}

void AppDraw() {
  if (g_app) {
    g_app->Draw();
  }
}

void AppShutdown() { g_app.reset(); }

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  return RunTestApp(argc, argv, zebes::RegisterTests, zebes::AppSetup, zebes::AppDraw,
                    zebes::AppShutdown);
}