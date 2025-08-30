#pragma once

#include "imgui.h"

namespace zebes {

bool IsMouseOverRect(const ImVec2 &position, const ImVec2 &size);

ImVec2 GetRelativePosition(const ImVec2 &offset_position, int scale);

}  // namespace zebes