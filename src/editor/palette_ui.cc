#include "editor/palette_ui.h"

#include <algorithm>
#include <cmath>

#include "absl/status/status.h"

namespace zebes {

bool PaletteGridLayout::ContinueRowAfter(int rendered_count) const {
  return rendered_count > 0 && columns > 0 && rendered_count % columns != 0;
}

absl::StatusOr<PaletteGridLayout> CalculatePaletteGridLayout(float available_width,
                                                             float item_width, float gap) {
  if (!std::isfinite(available_width) || !std::isfinite(item_width) || !std::isfinite(gap) ||
      available_width < 0.0f || item_width <= 0.0f || gap < 0.0f) {
    return absl::InvalidArgumentError("palette grid dimensions are invalid");
  }
  return PaletteGridLayout{
      .columns = std::max(1, static_cast<int>((available_width + gap) / (item_width + gap))),
  };
}

void DrawPaletteItemFrame(ImDrawList& draw_list, ImVec2 minimum, ImVec2 maximum, bool selected,
                          bool hovered) {
  if (hovered) {
    draw_list.AddRect(minimum, maximum, IM_COL32(210, 210, 210, 220), 0.0f, 0, 1.0f);
  }
  if (selected) {
    draw_list.AddRect(minimum, maximum, IM_COL32(60, 140, 255, 255), 0.0f, 0, 2.0f);
  }
}

}  // namespace zebes
