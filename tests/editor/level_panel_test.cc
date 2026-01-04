#include "editor/level_editor/level_panel.h"

#include <memory>

#include "imgui.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "tests/api_mock.h"
#include "tests/editor/test_main.h"

namespace zebes {
namespace {

using ::testing::NiceMock;

std::unique_ptr<MockApi> g_api;
std::unique_ptr<LevelPanel> g_level_panel;

void AppSetup() {
  g_api = std::make_unique<NiceMock<MockApi>>();
  auto result = LevelPanel::Create(g_api.get());
  if (!result.ok()) {
    abort();
  }
  g_level_panel = std::move(result.value());
}

void AppShutdown() {
  g_level_panel.reset();
  g_api.reset();
}

void AppDraw() {
  if (g_level_panel) {
    ImGui::SetNextWindowSize(ImVec2(1024, 768), ImGuiCond_Appearing);
    ImGui::Begin("Test Window", nullptr, ImGuiWindowFlags_None);
    g_level_panel->Render();
    ImGui::End();
  }
}

// Register ImGui Test Engine tests.
void RegisterTests(ImGuiTestEngine* engine) {
  IM_REGISTER_TEST(engine, "level_panel", "basic_render")->TestFunc = [](ImGuiTestContext* ctx) {
    ctx->SetRef("Test Window");

    // Verify Create button exists (nested under LevelPanel ID)
    IM_CHECK(ctx->ItemExists("**/Create"));

    // Verify Attach button exists
    IM_CHECK(ctx->ItemExists("**/Attach"));

    // Verify Delete button exists
    IM_CHECK(ctx->ItemExists("**/Delete"));
  };
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  return RunTestApp(argc, argv, zebes::RegisterTests, zebes::AppSetup, zebes::AppDraw,
                    zebes::AppShutdown);
}
