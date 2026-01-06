#include "editor/level_editor/parallax_panel.h"

#include <memory>
#include <vector>

#include "imgui.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "objects/level.h"
#include "tests/editor/test_main.h"

namespace zebes {
namespace {

std::unique_ptr<ParallaxPanel> g_parallax_panel;
Level g_level;
absl::StatusOr<ParallaxResult> g_last_render_result;

void AppSetup() {
  g_parallax_panel = std::make_unique<ParallaxPanel>();
  g_level = Level{.id = "test_level", .name = "Test Level"};
}

void AppShutdown() { g_parallax_panel.reset(); }

void AppDraw() {
  if (g_parallax_panel) {
    ImGui::SetNextWindowSize(ImVec2(1024, 768), ImGuiCond_Appearing);
    ImGui::Begin("Test Window", nullptr, ImGuiWindowFlags_None);
    g_last_render_result = g_parallax_panel->Render(g_level);
    ImGui::End();
  }
}

// Register ImGui Test Engine tests.
void RegisterTests(ImGuiTestEngine* engine) {
  IM_REGISTER_TEST(engine, "parallax_panel", "basic_render")->TestFunc = [](ImGuiTestContext* ctx) {
    ctx->SetRef("Test Window");

    // Verify Create button exists
    IM_CHECK(ctx->ItemExists("**/Create"));

    // Ensure Render returns OK
    IM_CHECK(g_last_render_result.ok());
  };

  IM_REGISTER_TEST(engine, "parallax_panel", "create_layer")->TestFunc = [](ImGuiTestContext* ctx) {
    g_level.parallax_layers.clear();
    ctx->SetRef("Test Window");

    // Click Create
    ctx->ItemClick("**/Create");
    ctx->Yield();

    // Should be in Details view now
    IM_CHECK(ctx->ItemExists("**/Name"));
    IM_CHECK(ctx->ItemExists("**/Texture ID"));
    IM_CHECK(ctx->ItemExists("**/Save"));

    // Save
    ctx->ItemClick("**/Save");
    ctx->Yield();

    // Should be back to List view
    IM_CHECK(ctx->ItemExists("**/Create"));

    // Check if layer was added
    IM_CHECK(g_level.parallax_layers.size() == 1);
    IM_CHECK(g_level.parallax_layers[0].name == "Layer 0");
  };

  IM_REGISTER_TEST(engine, "parallax_panel", "edit_layer")->TestFunc = [](ImGuiTestContext* ctx) {
    g_level.parallax_layers.clear();
    g_level.parallax_layers.push_back({.name = "Background", .texture_id = "tex_bg"});
    ctx->SetRef("Test Window");

    // Select the layer (it's in a list box)
    // We need to click "Edit", but first we must select the item in the listbox.
    // However, the listbox items are selectables.
    // The panel code:
    // if (ImGui::Selectable(layer.name.c_str(), is_selected)) { selected_index_ = i; }

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
    ctx->ItemInputValue("**/Texture ID", "new_tex");

    // Save
    ctx->ItemClick("**/Save");
    ctx->Yield();

    // Verify change
    IM_CHECK(g_level.parallax_layers.size() == 1);
    IM_CHECK(g_level.parallax_layers[0].name == "New Background");
    IM_CHECK(g_level.parallax_layers[0].texture_id == "new_tex");
  };

  IM_REGISTER_TEST(engine, "parallax_panel", "delete_layer")->TestFunc = [](ImGuiTestContext* ctx) {
    g_level.parallax_layers.clear();
    g_level.parallax_layers.push_back({.name = "Layer To Delete"});
    ctx->SetRef("Test Window");

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
