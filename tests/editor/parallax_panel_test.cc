#include "editor/level_editor/parallax_panel.h"

#include <memory>
#include <vector>

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
std::unique_ptr<ParallaxPanel> g_parallax_panel;
Level g_level;
std::vector<std::string> g_textures = {"tex1", "tex2", "tex_bg", "new_tex"};
absl::StatusOr<ParallaxResult> g_last_render_result;

void AppSetup() {
  g_api = std::make_unique<NiceMock<MockApi>>();
  std::vector<Texture> textures;
  for (const auto& t : g_textures) {
    textures.push_back(Texture{.id = t});
  }
  ON_CALL(*g_api, GetAllTextures()).WillByDefault(Return(textures));

  auto panel_or = ParallaxPanel::Create({.api = g_api.get()});
  if (!panel_or.ok()) {
    LOG(FATAL) << "Parallax panel failed to create";
  }
  g_parallax_panel = *std::move(panel_or);
  // g_level = Level{.id = "test_level", .name = "Test Level"};
  g_level = Level();
  g_level.id = "test_level";
  g_level.name = "Test Level";
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
    IM_CHECK(ctx->ItemExists("**/Create"));

    // Ensure Render returns OK
    IM_CHECK(g_last_render_result.ok());
  };

  IM_REGISTER_TEST(engine, "parallax_panel", "create_layer")->TestFunc = [](ImGuiTestContext* ctx) {
    auto panel_or = ParallaxPanel::Create({.api = g_api.get()});
    IM_CHECK(panel_or.ok());
    g_parallax_panel = *std::move(panel_or);  // Reset state
    g_level.parallax_layers.clear();
    ctx->SetRef(kAppWindowName);

    // Click Create
    ctx->ItemClick("**/Create");
    ctx->Yield();

    // Should be in Details view now
    IM_CHECK(ctx->ItemExists("**/Name"));
    IM_CHECK(ctx->ItemExists("**/Save"));

    // Select a texture (required for validation)
    if (ctx->ItemExists("**/tex1")) {
      ctx->ItemClick("**/tex1");
    }
    ctx->Yield();

    // Save
    ctx->ItemClick("**/Save");
    ctx->Yield();

    // Should be back to List view
    IM_CHECK(ctx->ItemExists("**/Create"));

    // Check if layer was added
    IM_CHECK(g_level.parallax_layers.size() == 1);
    IM_CHECK(g_level.parallax_layers[0].name == "Layer 0");
    IM_CHECK(g_level.parallax_layers[0].texture_id == "tex1");
  };

  IM_REGISTER_TEST(engine, "parallax_panel", "edit_layer")->TestFunc = [](ImGuiTestContext* ctx) {
    auto panel_or = ParallaxPanel::Create({.api = g_api.get()});
    IM_CHECK(panel_or.ok());
    g_parallax_panel = *std::move(panel_or);  // Reset state
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
    IM_CHECK(ctx->ItemExists("**/Name"));

    // Change name
    ctx->ItemInputValue("**/Name", "New Background");

    // Select new texture
    ctx->ItemClick("**/new_tex");
    ctx->Yield();

    // Save
    ctx->ItemClick("**/Save");
    ctx->Yield();

    // Verify change
    IM_CHECK(g_level.parallax_layers.size() == 1);
    IM_CHECK(g_level.parallax_layers[0].name == "New Background");
    IM_CHECK(g_level.parallax_layers[0].texture_id == "new_tex");
  };

  IM_REGISTER_TEST(engine, "parallax_panel", "delete_layer")->TestFunc = [](ImGuiTestContext* ctx) {
    auto panel_or = ParallaxPanel::Create({.api = g_api.get()});
    IM_CHECK(panel_or.ok());
    g_parallax_panel = *std::move(panel_or);  // Reset state
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
    IM_CHECK(g_level.parallax_layers.empty());
  };
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  return RunTestApp(argc, argv, zebes::RegisterTests, zebes::AppSetup, zebes::AppDraw,
                    zebes::AppShutdown);
}
