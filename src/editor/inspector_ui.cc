#include "editor/inspector_ui.h"

#include <cfloat>

#include "imgui.h"

namespace zebes {
namespace {

constexpr ImGuiTableFlags kPropertyTableFlags =
    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX;

}  // namespace

void RenderInspectorSection(GuiInterface& gui, const char* title, const char* description) {
  gui.Spacing();
  gui.Separator();
  gui.Text("%s", title);
  if (description != nullptr && description[0] != '\0') gui.TextWrapped("%s", description);
}

InspectorPropertyGrid::InspectorPropertyGrid(GuiInterface& gui, const char* id, float label_width)
    : gui_(gui), table_(gui.CreateScopedTable(id, 2, kPropertyTableFlags)) {
  if (!table_) return;
  gui_.TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, label_width);
  gui_.TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
}

bool InspectorPropertyGrid::BeginRow(const char* label, const char* help) {
  if (!table_) return false;
  gui_.TableNextRow();
  gui_.TableNextColumn();
  gui_.AlignTextToFramePadding();
  gui_.Text("%s", label);
  if (help != nullptr && help[0] != '\0' && gui_.IsItemHovered()) {
    gui_.SetTooltip("%s", help);
  }
  gui_.TableNextColumn();
  gui_.SetNextItemWidth(-FLT_MIN);
  return true;
}

}  // namespace zebes
