#include "editor/level_editor/level_panel.h"

#include <memory>

#include "imgui.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "tests/api_mock.h"
#include "tests/editor/test_main.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

std::unique_ptr<MockApi> g_api;
std::unique_ptr<LevelPanel> g_level_panel;
absl::StatusOr<LevelResult> g_last_render_result;

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
    g_last_render_result = g_level_panel->Render();
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

    // Ensure Render returns OK
    IM_CHECK(g_last_render_result.ok());
  };

  IM_REGISTER_TEST(engine, "level_panel", "create_transfer")->TestFunc = [](ImGuiTestContext* ctx) {
    ctx->SetRef("Test Window");

    // Setup Mock behavior
    EXPECT_CALL(*g_api, CreateLevel(_)).WillRepeatedly(Return("test_level_id"));
    // Use Invoke to return a fresh move-only vector each time
    EXPECT_CALL(*g_api, GetAllLevels()).WillRepeatedly(testing::Invoke([]() {
      return std::vector<Level>{};
    }));

    // Perform Click
    ctx->ItemClick("**/Create");

    // Wait for frame to process
    ctx->Yield();

    // Verify Render returned OK
    IM_CHECK(g_last_render_result.ok());

    // Verify we are now in Details View (Check for Save/Detach/Reset buttons)
    IM_CHECK(ctx->ItemExists("**/Save"));
    IM_CHECK(ctx->ItemExists("**/Detach"));
    IM_CHECK(ctx->ItemExists("**/Reset"));
  };
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  return RunTestApp(argc, argv, zebes::RegisterTests, zebes::AppSetup, zebes::AppDraw,
                    zebes::AppShutdown);
}
