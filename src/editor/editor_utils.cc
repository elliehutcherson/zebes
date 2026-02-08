#include "editor/editor_utils.h"

#include "editor/gui_interface.h"
#include "imgui.h"

namespace zebes {

float CalculateButtonWidth(GuiInterface* gui, int num_buttons) {
  if (num_buttons <= 0) return 0.0f;

  float avail_width = gui->GetContentRegionAvail().x;
  float total_spacing = gui->GetStyle().ItemSpacing.x * (num_buttons - 1);
  return (avail_width - total_spacing) / num_buttons;
}

}  // namespace zebes
