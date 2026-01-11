#include "editor/level_editor/level_panel.h"

#include <memory>
#include <optional>

#include "imgui.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "objects/level.h"
#include "tests/api_mock.h"
#include "tests/editor/test_main.h"

namespace zebes {
namespace {

using ::testing::NiceMock;
using ::testing::Return;

std::unique_ptr<MockApi> g_api;
std::unique_ptr<LevelPanel> g_level_panel;
std::optional<Level> g_level;
absl::StatusOr<LevelResult> g_last_render_result;

void AppSetup() {
  g_api = std::make_unique<NiceMock<MockApi>>();
  ON_CALL(*g_api, GetAllLevels()).WillByDefault([]() {
    std::vector<Level> levels;
    levels.push_back(Level{.name = "Level 1"});
    levels.push_back(Level{.name = "Level 2"});
    return levels;
  });
  g_level = std::nullopt;

  LevelPanel::Options options;
  options.api = g_api.get();
  auto panel_or = LevelPanel::Create(options);
  if (!panel_or.ok()) {
    abort();
  }
  g_level_panel = std::move(*panel_or);
}

void AppShutdown() {
  g_level_panel.reset();
  g_api.reset();
}

void AppDraw() {
  if (g_level_panel) {
    g_last_render_result = g_level_panel->Render(g_level);
  }
}

// Register ImGui Test Engine tests.
void RegisterTests(ImGuiTestEngine* engine) {
  IM_REGISTER_TEST(engine, "level_panel", "basic_render")->TestFunc = [](ImGuiTestContext* ctx) {
    ctx->SetRef(kAppWindowName);

    // Initial state: Level is null, so Create/Edit/Delete buttons should be visible
    IM_CHECK(ctx->ItemExists("**/Create"));
    IM_CHECK(ctx->ItemExists("**/Edit"));
    IM_CHECK(ctx->ItemExists("**/Delete"));

    // Ensure Render returns OK
    IM_CHECK(g_last_render_result.ok());
  };

  IM_REGISTER_TEST(engine, "level_panel", "create_click")->TestFunc = [](ImGuiTestContext* ctx) {
    // Reset state (simulating a fresh start or ensure we are on list view)
    g_level = std::nullopt;
    ctx->SetRef(kAppWindowName);

    // Verify initial counters
    auto initial_counters = g_level_panel->GetCounters();

    // Click Create
    ctx->ItemClick("**/Create");

    // Check that we clicked Create via counters
    auto new_counters = g_level_panel->GetCounters();
    IM_CHECK_EQ(new_counters.create, initial_counters.create + 1);

    // Level should be created and view switched (though checking counters is the main goal)
    IM_CHECK(g_level.has_value());
  };

  IM_REGISTER_TEST(engine, "level_panel", "edit_click")->TestFunc = [](ImGuiTestContext* ctx) {
    g_level = std::nullopt;
    ctx->SetRef(kAppWindowName);

    auto initial_counters = g_level_panel->GetCounters();

    // Ensure we have a selection (default is 0)
    IM_CHECK_EQ(g_level_panel->TestOnly_GetSelectedIndex(), 0);

    ctx->ItemClick("**/Edit");

    auto new_counters = g_level_panel->GetCounters();
    IM_CHECK_EQ(new_counters.edit, initial_counters.edit + 1);

    // Verify that clicking Edit opened the level (g_level is populated)
    IM_CHECK(g_level.has_value());
    IM_CHECK_STR_EQ(g_level->name.c_str(), "Level 1");
  };

  IM_REGISTER_TEST(engine, "level_panel", "delete_click")->TestFunc = [](ImGuiTestContext* ctx) {
    g_level = std::nullopt;
    ctx->SetRef(kAppWindowName);

    auto initial_counters = g_level_panel->GetCounters();

    ctx->ItemClick("**/Delete");

    auto new_counters = g_level_panel->GetCounters();
    IM_CHECK_EQ(new_counters.del, initial_counters.del + 1);
  };

  IM_REGISTER_TEST(engine, "level_panel", "select_level")->TestFunc = [](ImGuiTestContext* ctx) {
    g_level = std::nullopt;
    ctx->SetRef(kAppWindowName);

    // Initial state: Level 1 selected (index 0)
    IM_CHECK_EQ(g_level_panel->TestOnly_GetSelectedIndex(), 0);

    // Select Level 2
    ctx->ItemClick("**/Level 2");

    IM_CHECK_EQ(g_level_panel->TestOnly_GetSelectedIndex(), 1);

    // Check that selecting a new level DOES NOT auto-open it
    IM_CHECK(!g_level.has_value());
  };

  IM_REGISTER_TEST(engine, "level_panel", "back_click")->TestFunc = [](ImGuiTestContext* ctx) {
    g_level = Level{.id = "test_level", .name = "Test Level"};
    ctx->SetRef(kAppWindowName);

    auto initial_counters = g_level_panel->GetCounters();
    ctx->ItemClick("**/Back");

    auto new_counters = g_level_panel->GetCounters();
    IM_CHECK_EQ(new_counters.back, initial_counters.back + 1);
    IM_CHECK(!g_level.has_value());
  };

  IM_REGISTER_TEST(engine, "level_panel", "save_click")->TestFunc = [](ImGuiTestContext* ctx) {
    g_level = Level{.id = "test_level", .name = "Test Level"};
    ctx->SetRef(kAppWindowName);

    auto initial_counters = g_level_panel->GetCounters();
    ctx->ItemClick("**/Save");

    auto new_counters = g_level_panel->GetCounters();
    IM_CHECK_EQ(new_counters.save, initial_counters.save + 1);
  };
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  return RunTestApp(argc, argv, zebes::RegisterTests, zebes::AppSetup, zebes::AppDraw,
                    zebes::AppShutdown);
}
