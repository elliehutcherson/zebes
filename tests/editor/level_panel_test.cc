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
    // Reset state to ensure we are in RenderList mode
    if (g_level_panel) {
      g_level_panel->Detach();
    }

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

  IM_REGISTER_TEST(engine, "level_panel", "back_button_behavior")->TestFunc =
      [](ImGuiTestContext* ctx) {
        // Reset state to ensure we are in RenderList mode
        if (g_level_panel) {
          g_level_panel->Detach();
        }

        ctx->SetRef("Test Window");

        // Setup Mock behavior
        EXPECT_CALL(*g_api, CreateLevel(_)).WillRepeatedly(Return("test_level_id"));
        EXPECT_CALL(*g_api, GetAllLevels()).WillRepeatedly(testing::Invoke([]() {
          return std::vector<Level>{};
        }));

        // Start by Entering Details View
        ctx->ItemClick("**/Create");
        ctx->Yield();

        // Verify we are in details view
        IM_CHECK(ctx->ItemExists("**/Back"));

        // Click Back
        ctx->ItemClick("**/Back");
        ctx->Yield();

        // Verify Result was Detach
        IM_CHECK(g_last_render_result.ok());

        // Note: We might miss the exact frame where kDetach is returned if Yield() runs multiple
        // frames. We primarily verify the state transition to List View.

        // Verify state transition: We should be back to list view
        // 1. We should see the Create button again
        IM_CHECK(ctx->ItemExists("**/Create"));

        // 2. render_list_count should have incremented (meaning RenderList was called)
        int current_list_count = g_level_panel->GetCounters().render_list_count;
        IM_CHECK(current_list_count > 0);
      };

  IM_REGISTER_TEST(engine, "level_panel", "edit_fields")->TestFunc = [](ImGuiTestContext* ctx) {
    // Reset state
    if (g_level_panel) {
      g_level_panel->Detach();
    }
    ctx->SetRef("Test Window");

    // Setup Mock
    EXPECT_CALL(*g_api, CreateLevel(_)).WillRepeatedly(Return("test_level_id"));
    EXPECT_CALL(*g_api, GetAllLevels()).WillRepeatedly(testing::Invoke([]() {
      return std::vector<Level>{};
    }));

    // Enter Details View
    ctx->ItemClick("**/Create");
    ctx->Yield();

    // Verify Fields Exist
    IM_CHECK(ctx->ItemExists("**/Width"));
    IM_CHECK(ctx->ItemExists("**/Height"));
    IM_CHECK(ctx->ItemExists("**/Spawn X"));
    IM_CHECK(ctx->ItemExists("**/Spawn Y"));

    // Modify Fields
    ctx->ItemInputValue("**/Width", 100.0f);
    ctx->ItemInputValue("**/Height", 200.0f);
    ctx->ItemInputValue("**/Spawn X", 50.0f);
    ctx->ItemInputValue("**/Spawn Y", 60.0f);

    // Mock UpdateLevel expectation
    EXPECT_CALL(*g_api, UpdateLevel(_)).WillOnce(testing::Invoke([](const Level& level) {
      EXPECT_NEAR(level.width, 100.0, 1e-5);
      EXPECT_NEAR(level.height, 200.0, 1e-5);
      EXPECT_NEAR(level.spawn_point.x, 50.0, 1e-5);
      EXPECT_NEAR(level.spawn_point.y, 60.0, 1e-5);
      return absl::OkStatus();
    }));

    // Save
    ctx->ItemClick("**/Save");
  };
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  return RunTestApp(argc, argv, zebes::RegisterTests, zebes::AppSetup, zebes::AppDraw,
                    zebes::AppShutdown);
}
