#pragma once

#include "absl/status/statusor.h"
#include "imgui.h"

namespace zebes {

struct PaletteGridLayout {
  int columns = 1;

  // rendered_count is the number of items already rendered, including the
  // current item. True means the next item belongs on the same row.
  bool ContinueRowAfter(int rendered_count) const;
};

// Computes a packed horizontal grid. Item width and gap are presentation
// inputs, while column count and row continuation are shared panel behavior.
absl::StatusOr<PaletteGridLayout> CalculatePaletteGridLayout(float available_width,
                                                             float item_width, float gap);

// Shared hover/selection chrome for visual palette items.
void DrawPaletteItemFrame(ImDrawList& draw_list, ImVec2 minimum, ImVec2 maximum, bool selected,
                          bool hovered);

}  // namespace zebes
