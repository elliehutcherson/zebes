#include "editor/level_editor/level_panel.h"

#include <memory>

#include "gmock/gmock.h"
#include "imgui.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "tests/api_mock.h"
#include "tests/editor/test_main.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

// ============================================================================
// Test Fixture
// ============================================================================

struct LevelPanelTestFixture {
  std::unique_ptr<MockApi> api = std::make_unique<NiceMock<MockApi>>();
  std::unique_ptr<LevelPanel> panel;
  std::optional<Level> editing_level;
  absl::StatusOr<LevelResult> last_render_result;
  std::vector<Level> levels;

  // Call counters for verification
  struct CallCounts {
    int create_level = 0;
    int get_all_levels = 0;
    int save_level = 0;
    int delete_level = 0;
    int attach_level = 0;

    void Reset() {
      create_level = 0;
      get_all_levels = 0;
      save_level = 0;
      delete_level = 0;
      attach_level = 0;
    }
  } call_counts;

  // Initialize the test fixture with a fresh mock API
  void SetupMocks() {
    // Passing the lambda directly to WillByDefault
    ON_CALL(*api, CreateLevel).WillByDefault([this](const Level& level) {
      call_counts.create_level++;
      return "test_level_id";
    });

    ON_CALL(*api, GetAllLevels).WillByDefault([this]() {
      call_counts.get_all_levels++;
      std::vector<Level> levels;
      for (const Level& l : levels) {
        levels.push_back(l.GetCopy());
      }
      return levels;
    });
  }

  // Full reset for simple tests
  void Reset() {
    editing_level.reset();
    call_counts.Reset();
    SetupMocks();

    auto result = LevelPanel::Create({
        .api = api.get(),
        .editting_level = &editing_level,
    });

    if (!result.ok()) {
      LOG(FATAL) << "Failed to create LevelPanel: " << result.status();
    }
    panel = std::move(result.value());
  }

  // Render the panel
  // Render the panel
  void Render() {
    if (panel == nullptr) {
      LOG(FATAL) << "Panel is null during render!!";
    }

    last_render_result = panel->Render();
  }

  void Cleaup() { api.reset(); }
};

// Global fixture instance for simple test runner integration
static LevelPanelTestFixture g_fixture;

// ============================================================================
// Test Runner Callbacks
// ============================================================================

void AppSetup() {
  LOG(INFO) << "Setup...";
  // Reset before each test
  g_fixture.Reset();
}

void AppShutdown() {
  g_fixture.Cleaup();
  LOG(INFO) << "teardown...";
}

void AppDraw() { g_fixture.Render(); }

// ============================================================================
// Test Registration
// ============================================================================

void RegisterTests(ImGuiTestEngine* engine) {
  ImGuiTest* t = nullptr;

  // --------------------------------------------------------------------------
  // Basic Render Test
  // --------------------------------------------------------------------------
  // t = IM_REGISTER_TEST(engine, "level_panel", "basic_render");
  // t->TestFunc = [](ImGuiTestContext* ctx) {
  //   ctx->SetRef(kAppWindowName);

  //   // Verify main action buttons exist
  //   IM_CHECK(ctx->ItemExists("**/Create"));
  //   IM_CHECK(ctx->ItemExists("**/Attach"));
  //   IM_CHECK(ctx->ItemExists("**/Delete"));

  //   // Verify render succeeded
  //   IM_CHECK(g_fixture.last_render_result.ok());

  //   // Verify no API calls were made during basic render
  //   IM_CHECK_EQ(g_fixture.call_counts.create_level, 0);
  //   IM_CHECK_EQ(g_fixture.call_counts.get_all_levels, 1);
  // };

  // --------------------------------------------------------------------------
  // Create Level Transfer Test
  // --------------------------------------------------------------------------
  t = IM_REGISTER_TEST(engine, "level_panel", "create_transfer");
  t->TestFunc = [](ImGuiTestContext* ctx) {
    // 1. Set context to the window
    g_fixture.Reset();
    ctx->Yield();  // Wait for AppDraw to render with new panel
    ctx->SetRef(kAppWindowName);
    ctx->WindowFocus(kAppWindowName);

    // Verify Create button exists and click it
    IM_CHECK(ctx->ItemExists("**/Create"));
    ctx->ScrollToItem("**/Create", ImGuiAxis_Y);
    ctx->ItemClick("**/Create");
    ctx->Yield();

    // 6. Verify the result
    IM_CHECK_EQ(g_fixture.call_counts.create_level, 1);

    IM_CHECK(g_fixture.last_render_result.ok());
    IM_CHECK(ctx->ItemExists("**/Save"));
    IM_CHECK(ctx->ItemExists("**/Detach"));
    IM_CHECK(ctx->ItemExists("**/Reset"));
  };
}

}  // namespace
}  // namespace zebes

// ============================================================================
// Main Entry Point
// ============================================================================

// int main(int argc, char** argv) { return RunTestApp(argc, argv, RegisterTests); }
int main(int argc, char** argv) {
  return RunTestApp(argc, argv, zebes::RegisterTests, zebes::AppSetup, zebes::AppDraw,
                    zebes::AppShutdown);
}