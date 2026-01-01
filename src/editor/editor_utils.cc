#include "editor/editor_utils.h"

#include "imgui.h"

namespace zebes {

float CalculateButtonWidth(int num_buttons) {
  if (num_buttons <= 0) return 0.0f;

  float avail_width = ImGui::GetContentRegionAvail().x;
  float total_spacing = ImGui::GetStyle().ItemSpacing.x * (num_buttons - 1);
  return (avail_width - total_spacing) / num_buttons;
}

}  // namespace zebes
