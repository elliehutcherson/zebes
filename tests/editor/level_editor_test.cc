#include "editor/level_editor/level_editor.h"

#include <memory>

#include "imgui.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "tests/api_mock.h"
#include "tests/editor/test_main.h"

namespace zebes {
namespace {

using ::testing::NiceMock;

// Application state for the test.
class LevelEditorTestApp {
 public:
  LevelEditorTestApp() {
    // Use MockApi to avoid complex setup of dependencies.
    api_ = std::make_unique<NiceMock<MockApi>>();

    // Create LevelEditor instance.
    auto result = LevelEditor::Create(api_.get());
    // Use ABORT/assert for critical setup failures in the test app constructor.
    if (!result.ok()) {
      abort();
    }
    level_editor_ = std::move(result.value());
  }

  void Draw() { level_editor_->Render(); }

 private:
  std::unique_ptr<MockApi> api_;
  std::unique_ptr<LevelEditor> level_editor_;
};

// Global instance of the test app.
std::unique_ptr<LevelEditorTestApp> g_app;

void AppSetup() { g_app = std::make_unique<LevelEditorTestApp>(); }

void AppShutdown() { g_app.reset(); }

void AppDraw() {
  if (g_app) {
    // printf("AppDraw called\n"); // Debug logging
    // Render the Level Editor within a test window.
    // Use a fixed size to ensure the table layout (which uses stretch columns) has space to render.
    ImGui::SetNextWindowSize(ImVec2(1024, 768), ImGuiCond_Appearing);
    ImGui::Begin("Test Window", nullptr, ImGuiWindowFlags_None);
    g_app->Draw();
    ImGui::End();
  }
}

// Register ImGui Test Engine tests.
void RegisterTests(ImGuiTestEngine* engine) {
  IM_REGISTER_TEST(engine, "level_editor", "test_layout")->TestFunc = [](ImGuiTestContext* ctx) {
    // 1. Setup: Set reference window.
    ctx->SetRef("Test Window");

    // Debugging: Verify test engine is running.
    IM_CHECK(true);

    // 2. Verify Table Existence.
    IM_CHECK_SILENT(ctx->ItemExists("**/LevelEditorLayout"));

    // 3. Verify Left Panel (Level List).
    IM_CHECK_SILENT(ctx->ItemExists("**/LevelListPanel"));
    IM_CHECK_SILENT(ctx->ItemExists("**/Add Level"));

    // 4. Verify Center Panel (Viewport).
    IM_CHECK_SILENT(ctx->ItemExists("**/LevelViewport"));
  };
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  return RunTestApp(argc, argv, zebes::RegisterTests, zebes::AppSetup, zebes::AppDraw,
                    zebes::AppShutdown);
}
