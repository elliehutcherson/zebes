#include "editor/tileset_editor/tile_panel.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"

namespace zebes {
namespace {

// Display strings for TileShape, indexed by numeric value (0..25).
// Must stay in sync with the TileShape enum in src/objects/tileset.h.
constexpr const char* kTileShapeNames[] = {
    "None",                       // 0  kNone
    "Full Block",                 // 1  kFullBlock
    "Half Block Bottom",          // 2  kHalfBlockBottom
    "Half Block Top",             // 3  kHalfBlockTop
    "Half Block Left",            // 4  kHalfBlockLeft
    "Half Block Right",           // 5  kHalfBlockRight
    "Slope 45 Bottom-Left",       // 6  kSlope45BottomLeft
    "Slope 45 Bottom-Right",      // 7  kSlope45BottomRight
    "Slope 45 Top-Left",          // 8  kSlope45TopLeft
    "Slope 45 Top-Right",         // 9  kSlope45TopRight
    "Gentle Slope BL Lower",      // 10 kGentleSlopeBottomLeft_Lower
    "Gentle Slope BL Upper",      // 11 kGentleSlopeBottomLeft_Upper
    "Gentle Slope BR Lower",      // 12 kGentleSlopeBottomRight_Lower
    "Gentle Slope BR Upper",      // 13 kGentleSlopeBottomRight_Upper
    "Gentle Slope TL Lower",      // 14 kGentleSlopeTopLeft_Lower
    "Gentle Slope TL Upper",      // 15 kGentleSlopeTopLeft_Upper
    "Gentle Slope TR Lower",      // 16 kGentleSlopeTopRight_Lower
    "Gentle Slope TR Upper",      // 17 kGentleSlopeTopRight_Upper
    "Steep Slope BL Bottom",      // 18 kSteepSlopeBottomLeft_Bottom
    "Steep Slope BL Top",         // 19 kSteepSlopeBottomLeft_Top
    "Steep Slope BR Bottom",      // 20 kSteepSlopeBottomRight_Bottom
    "Steep Slope BR Top",         // 21 kSteepSlopeBottomRight_Top
    "Steep Slope TL Bottom",      // 22 kSteepSlopeTopLeft_Bottom
    "Steep Slope TL Top",         // 23 kSteepSlopeTopLeft_Top
    "Steep Slope TR Bottom",      // 24 kSteepSlopeTopRight_Bottom
    "Steep Slope TR Top",         // 25 kSteepSlopeTopRight_Top
};
constexpr int kTileShapeCount = static_cast<int>(std::size(kTileShapeNames));

}  // namespace

absl::StatusOr<std::unique_ptr<TilePanel>> TilePanel::Create(GuiInterface* gui) {
  if (gui == nullptr) return absl::InvalidArgumentError("Gui can not be null.");
  return absl::WrapUnique(new TilePanel(gui));
}

TilePanel::TilePanel(GuiInterface* gui) : gui_(gui) {}

absl::Status TilePanel::RenderDetails(Tile& tile) {
  gui_->InputText("Tile Name", &tile.name);
  gui_->InputInt("Source X", &tile.source_x);
  gui_->InputInt("Source Y", &tile.source_y);

  // Shape combo — indexed directly by the enum's numeric value.
  int shape_index = static_cast<int>(tile.shape);
  const char* shape_preview =
      (shape_index >= 0 && shape_index < kTileShapeCount)
          ? kTileShapeNames[shape_index]
          : "Unknown";
  if (ScopedCombo combo = gui_->CreateScopedCombo("Shape", shape_preview); combo) {
    for (int i = 0; i < kTileShapeCount; ++i) {
      bool is_selected = (shape_index == i);
      if (gui_->Selectable(kTileShapeNames[i], is_selected)) {
        tile.shape = static_cast<TileShape>(i);
      }
      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  gui_->Checkbox("One-Way Platform", &tile.is_one_way);

  // Tags are edited as a comma-separated string for simplicity.
  std::string tags_str = absl::StrJoin(tile.tags, ",");
  if (gui_->InputText("Tags", &tags_str)) {
    tile.tags = absl::StrSplit(tags_str, ',', absl::SkipEmpty());
  }

  return absl::OkStatus();
}

}  // namespace zebes
