#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "test_main.h"

void RegisterTests(ImGuiTestEngine* engine) {
  ImGuiTest* t = NULL;

  // Sanity Check
  t = IM_REGISTER_TEST(engine, "sanity", "basic_check");
  t->TestFunc = [](ImGuiTestContext* ctx) { IM_CHECK_EQ(1, 1); };
}

int main(int argc, char** argv) { return RunTestApp(argc, argv, RegisterTests); }
