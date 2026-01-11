#include "editor/level_editor/level_editor.h"

#include <memory>

#include "common/status_macros.h"
#include "imgui.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_level_panel.h"
#include "tests/editor/test_main.h"

namespace zebes {
namespace {

using ::testing::NiceMock;

using ::testing::_;
using ::testing::Return;

std::unique_ptr<MockApi> g_api;
std::unique_ptr<LevelEditor> g_level_editor;
MockLevelPanel* g_level_panel_mock = nullptr;

void AppSetup() {
  g_api = std::make_unique<NiceMock<MockApi>>();
  ON_CALL(*g_api, GetAllTextures()).WillByDefault(Return(std::vector<Texture>{}));

  auto mock_panel = std::make_unique<NiceMock<MockLevelPanel>>();
  g_level_panel_mock = mock_panel.get();

  // Setup default mock behavior
  ON_CALL(*g_level_panel_mock, Render(_)).WillByDefault(Return(LevelResult{}));

  LevelEditor::Options options;
  options.api = g_api.get();
  options.level_panel = std::move(mock_panel);

  auto result = LevelEditor::Create(std::move(options));
  if (!result.ok()) {
    LOG(ERROR) << "LevelEditor::Create failed: " << result.status();
    abort();
  }
  g_level_editor = std::move(result.value());
}

void AppShutdown() {
  g_level_editor.reset();
  g_api.reset();
}

void AppDraw() {
  if (g_level_editor) {
    auto status = g_level_editor->Render();
    if (!status.ok()) {
      LOG(ERROR) << "LevelEditor::Render failed: " << status;
    }
  }
}

// Register ImGui Test Engine tests.
void RegisterTests(ImGuiTestEngine* engine) {
  IM_REGISTER_TEST(engine, "level_editor", "basic_render")->TestFunc = [](ImGuiTestContext* ctx) {
    ctx->SetRef(kAppWindowName);

    // Verify LevelViewport window exists
    bool viewport_found = false;
    for (int i = 0; i < ctx->UiContext->Windows.Size; ++i) {
      if (strstr(ctx->UiContext->Windows[i]->Name, "LevelViewport") != nullptr) {
        viewport_found = true;
        break;
      }
    }
    IM_CHECK(viewport_found);

    // Verify MockLevelPanel::Render was called via checking if the mock behavior was triggered.
    // Since we can't easily assert on the mock object inside the lambda without global access to
    // the PROPER mock, and we're recreating the editor in AppSetup (which is called BEFORE
    // TestFunc), we need to ensure we access the mock. But AppSetup creates the editor. We need
    // to inject the mock. The AppSetup needs to use the global mocks.
  };

  using ::testing::Invoke;

  IM_REGISTER_TEST(engine, "level_editor", "mock_integration")->TestFunc =
      [](ImGuiTestContext* ctx) {
        // Program the mock to render a unique item
        EXPECT_CALL(*g_level_panel_mock, Render(_))
            .WillRepeatedly(Invoke([](std::optional<Level>&) -> absl::StatusOr<LevelResult> {
              ImGui::Button("MockLevelPanelContent");
              return LevelResult{};
            }));

        ctx->SetRef(kAppWindowName);
        ctx->Yield();

        // Verify that the LevelEditor called Render on our mock, and the mock drew its content.
        IM_CHECK(ctx->ItemExists("**/MockLevelPanelContent"));
      };
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  return RunTestApp(argc, argv, zebes::RegisterTests, zebes::AppSetup, zebes::AppDraw,
                    zebes::AppShutdown);
}
