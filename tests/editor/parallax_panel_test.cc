#include "editor/level_editor/parallax_panel.h"

#include <memory>
#include <vector>

#include "absl/log/log.h"
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

#define CHECK_AND_LOG(condition, message) \
  if (!(condition)) {                     \
    LOG(ERROR) << message;                \
  }                                       \
  IM_CHECK(condition)

std::unique_ptr<MockApi> g_api;
std::unique_ptr<ParallaxPanel> g_parallax_panel;
Level g_level;

std::vector<Texture> g_textures = {  // ID, Name, Path, SDL_Texture*
    {"t_001", "grass_ground", "assets/tiles/grass.png", nullptr},
    {"t_002", "stone_wall", "assets/tiles/stone.png", nullptr},
    {"t_003", "player_idle", "assets/chars/hero.png", nullptr},
    {"t_004", "enemy_slime", "assets/chars/slime.png", nullptr},
    {"t_005", "ui_button", "assets/ui/btn_ok.png", nullptr}};

absl::StatusOr<ParallaxResult> g_last_render_result;

void AppSetup() {
  g_api = std::make_unique<NiceMock<MockApi>>();
  ON_CALL(*g_api, GetAllTextures()).WillByDefault(Return(g_textures));

  auto panel = ParallaxPanel::Create({.api = g_api.get()});
  if (!panel.ok()) {
    LOG(FATAL) << "Parallax panel failed to create";
  }
  g_parallax_panel = *std::move(panel);
  g_level = Level{.id = "test_level", .name = "Test Level"};
}

void AppShutdown() {
  g_parallax_panel.reset();
  g_api.reset();
}

void AppDraw() {
  if (g_parallax_panel) {
    g_last_render_result = g_parallax_panel->Render(g_level);
  }
}

// Register ImGui Test Engine tests.
void RegisterTests(ImGuiTestEngine* engine) {
  IM_REGISTER_TEST(engine, "parallax_panel", "basic_render")->TestFunc = [](ImGuiTestContext* ctx) {
    ctx->SetRef(kAppWindowName);

    // Verify Create button exists
    CHECK_AND_LOG(ctx->ItemExists("**/Create"), "Create button missing in basic render");

    // Ensure Render returns OK
    CHECK_AND_LOG(g_last_render_result.ok(),
                  "Render returned error: " << g_last_render_result.status());
  };

  IM_REGISTER_TEST(engine, "parallax_panel", "create_layer")->TestFunc = [](ImGuiTestContext* ctx) {
    auto panel = ParallaxPanel::Create({.api = g_api.get()});
    CHECK_AND_LOG(panel.ok(), "ParallaxPanel::Create failed: " << panel.status());
    g_parallax_panel = *std::move(panel);  // Reset state
    g_level.parallax_layers.clear();
    ctx->SetRef(kAppWindowName);

    // Click Create
    ctx->ItemClick("**/Create");
    ctx->Yield();

    // Should be in Details view now
    CHECK_AND_LOG(ctx->ItemExists("**/Name"), "Name input missing after clicking Create");
    CHECK_AND_LOG(ctx->ItemExists("**/Save"), "Save button missing after clicking Create");

    // Select a texture (required for validation)
    // Texture format is "name-id"
    if (ctx->ItemExists("**/grass_ground-t_001")) {
      ctx->ItemClick("**/grass_ground-t_001");
      ctx->Yield();

      // Click Change Texture to confirm selection
      ctx->ItemClick("**/Change Texture");
      ctx->Yield();
    }

    // Save
    ctx->ItemClick("**/Save");
    ctx->Yield();

    // Check if layer was added
    CHECK_AND_LOG(g_level.parallax_layers.size() == 1,
                  "Expected 1 layer, got " << g_level.parallax_layers.size());
    if (!g_level.parallax_layers.empty()) {
      CHECK_AND_LOG(g_level.parallax_layers[0].name == "Layer 0",
                    "Expected name 'Layer 0', got '" << g_level.parallax_layers[0].name << "'");
      CHECK_AND_LOG(
          g_level.parallax_layers[0].texture_id == "t_001",
          "Expected texture_id 't_001', got '" << g_level.parallax_layers[0].texture_id << "'");
    }
  };

  IM_REGISTER_TEST(engine, "parallax_panel", "edit_layer")->TestFunc = [](ImGuiTestContext* ctx) {
    auto panel = ParallaxPanel::Create({.api = g_api.get()});
    CHECK_AND_LOG(panel.ok(), "ParallaxPanel::Create failed: " << panel.status());
    g_parallax_panel = *std::move(panel);  // Reset state
    g_level.parallax_layers.clear();
    g_level.parallax_layers.push_back({.name = "Background", .texture_id = "tex_bg"});
    ctx->SetRef(kAppWindowName);

    // Select the layer (it's in a list box)
    // We can just click the item by name
    ctx->ItemClick("**/Background");
    ctx->Yield();

    // Click Edit
    ctx->ItemClick("**/Edit");
    ctx->Yield();

    // Verify fields
    CHECK_AND_LOG(ctx->ItemExists("**/Name"), "Name input missing in Edit view");

    // Change name
    ctx->ItemInputValue("**/Name", "New Background");

    // Select new texture (stone_wall-t_002)
    ctx->ItemClick("**/stone_wall-t_002");
    ctx->Yield();

    // Click Change Texture
    ctx->ItemClick("**/Change Texture");
    ctx->Yield();

    // Save
    ctx->ItemClick("**/Save");
    ctx->Yield();

    // Verify change
    CHECK_AND_LOG(g_level.parallax_layers.size() == 1,
                  "Expected 1 layer, got " << g_level.parallax_layers.size());
    CHECK_AND_LOG(
        g_level.parallax_layers[0].name == "New Background",
        "Expected name 'New Background', got '" << g_level.parallax_layers[0].name << "'");
    CHECK_AND_LOG(
        g_level.parallax_layers[0].texture_id == "t_002",
        "Expected texture_id 't_002', got '" << g_level.parallax_layers[0].texture_id << "'");
  };

  IM_REGISTER_TEST(engine, "parallax_panel", "delete_layer")->TestFunc = [](ImGuiTestContext* ctx) {
    auto panel = ParallaxPanel::Create({.api = g_api.get()});
    CHECK_AND_LOG(panel.ok(), "ParallaxPanel::Create failed: " << panel.status());
    g_parallax_panel = *std::move(panel);  // Reset state
    g_level.parallax_layers.clear();
    g_level.parallax_layers.push_back({.name = "Layer To Delete"});
    ctx->SetRef(kAppWindowName);

    // Select the layer
    ctx->ItemClick("**/Layer To Delete");
    ctx->Yield();

    // Click Delete
    ctx->ItemClick("**/Delete");
    ctx->Yield();

    // Verify removal
    CHECK_AND_LOG(g_level.parallax_layers.empty(),
                  "Expected layers to be empty, got " << g_level.parallax_layers.size());
  };
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  return RunTestApp(argc, argv, zebes::RegisterTests, zebes::AppSetup, zebes::AppDraw,
                    zebes::AppShutdown);
}
