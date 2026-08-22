#include <cstring>
#include <memory>
#include <vector>

#include "editor/anchor_gizmo_renderer.h"
#include "editor/gui.h"
#include "editor/level_editor/blueprint_palette_panel.h"
#include "editor/level_editor/viewport_model.h"
#include "gmock/gmock.h"
#include "imgui.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "test_main.h"
#include "tests/api_mock.h"

namespace zebes {
namespace {

using ::testing::NiceMock;
using ::testing::Return;

struct BlueprintPaletteUiVars {
  NiceMock<MockApi> api;
  Gui gui;
  Blueprint blueprint{
      .id = "crystal-blueprint",
      .name = "Cave Crystal",
      .states = {{.name = "Default", .placement_mode = BlueprintPlacementMode::kGrounded}},
  };
  std::unique_ptr<BlueprintPalettePanel> panel;
  absl::Status render_status;
  absl::Status gizmo_status;
  int gizmo_vertices_added = 0;
  bool initialized = false;
};

void RegisterTests(ImGuiTestEngine* engine) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "blueprint_palette", "select_and_place");
  test->SetVarsDataType<BlueprintPaletteUiVars>();
  test->GuiFunc = [](ImGuiTestContext* context) {
    BlueprintPaletteUiVars& vars = context->GetVars<BlueprintPaletteUiVars>();
    if (!vars.initialized) {
      ON_CALL(vars.api, GetAllBlueprints())
          .WillByDefault(Return(std::vector<Blueprint>{vars.blueprint}));
      ON_CALL(vars.api, GetBlueprint(vars.blueprint.id)).WillByDefault(Return(&vars.blueprint));
      auto panel = BlueprintPalettePanel::Create({.api = &vars.api, .gui = &vars.gui});
      IM_CHECK(panel.ok());
      if (panel.ok()) vars.panel = *std::move(panel);
      vars.initialized = true;
    }

    ImGui::SetNextWindowSize({1100.0F, 600.0F}, ImGuiCond_Always);
    ImGui::Begin("Blueprint Palette Test", nullptr, ImGuiWindowFlags_NoSavedSettings);
    if (vars.panel != nullptr) vars.render_status = vars.panel->Render();
    ImGui::End();
  };
  test->TestFunc = [](ImGuiTestContext* context) {
    BlueprintPaletteUiVars& vars = context->GetVars<BlueprintPaletteUiVars>();
    context->Yield();
    ImGuiWindow* grid_window = nullptr;
    for (ImGuiWindow* window : context->UiContext->Windows) {
      if (std::strstr(window->Name, "/BlueprintGrid_") != nullptr) {
        grid_window = window;
        break;
      }
    }
    IM_CHECK(grid_window != nullptr);
    if (grid_window == nullptr) return;

    ImGuiTestItemList items;
    context->GatherItems(&items, grid_window->ID);
    ImGuiID card_id = 0;
    for (int index = 0; index < items.GetSize(); ++index) {
      const ImGuiTestItemInfo* item = items.GetByIndex(index);
      if (std::strcmp(item->DebugLabel, "##blueprint") != 0) continue;
      IM_CHECK(card_id == 0);
      card_id = item->ID;
    }
    IM_CHECK(card_id != 0);
    if (card_id == 0) return;

    context->ItemClick(card_id);

    IM_CHECK(vars.render_status.ok());
    const Blueprint* selected = vars.panel->GetSelectedBlueprint();
    IM_CHECK(selected != nullptr);
    if (selected == nullptr) return;

    const Entity placed = CreateEntityFromBlueprint(*selected, 0, {48, 64}, 7);
    IM_CHECK_EQ(placed.blueprint_id, vars.blueprint.id);
    IM_CHECK_EQ(placed.transform.position.x, 48.0);
    IM_CHECK_EQ(placed.transform.position.y, 64.0);
  };

  ImGuiTest* gizmo_test = IM_REGISTER_TEST(engine, "anchor_gizmo", "presentation");
  gizmo_test->SetVarsDataType<BlueprintPaletteUiVars>();
  gizmo_test->GuiFunc = [](ImGuiTestContext* context) {
    BlueprintPaletteUiVars& vars = context->GetVars<BlueprintPaletteUiVars>();
    ImGui::Begin("Anchor Gizmo Test", nullptr, ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const int vertices_before = draw_list->VtxBuffer.Size;
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    vars.gizmo_status =
        DrawAnchorGizmo(*draw_list, {cursor.x, cursor.y}, BlueprintPlacementMode::kGrounded);
    vars.gizmo_vertices_added = draw_list->VtxBuffer.Size - vertices_before;
    ImGui::Dummy({32.0F, 32.0F});
    ImGui::End();
  };
  gizmo_test->TestFunc = [](ImGuiTestContext* context) {
    BlueprintPaletteUiVars& vars = context->GetVars<BlueprintPaletteUiVars>();
    context->Yield();
    IM_CHECK(vars.gizmo_status.ok());
    IM_CHECK_GT(vars.gizmo_vertices_added, 0);
  };
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) { return RunTestApp(argc, argv, zebes::RegisterTests); }
