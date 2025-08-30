#include "hud/utils.h"

#include "imgui.h"

namespace zebes {

bool IsMouseOverRect(const ImVec2 &position, const ImVec2 &size) {
  ImGuiIO &io = ImGui::GetIO();
  ImVec2 mouse_position = io.MousePos;
  return mouse_position.x >= position.x &&
         mouse_position.x <= position.x + size.x &&
         mouse_position.y >= position.y &&
         mouse_position.y <= position.y + size.y;
}

ImVec2 GetRelativePosition(const ImVec2 &offset_position, int scale) {
  ImGuiIO &io = ImGui::GetIO();
  ImVec2 mouse_position = io.MousePos;
  ImVec2 relative_position;
  relative_position.x = (mouse_position.x - offset_position.x) / scale;
  relative_position.y = (mouse_position.y - offset_position.y) / scale;
  return relative_position;
}

}  // namespace zebes
