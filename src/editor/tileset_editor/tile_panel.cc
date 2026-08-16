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
//
// Phrased the same way as the terrain palette's catalogue
// (terrain/terrain_placement.cc), because both panels name the same shapes and
// two vocabularies for one set of wedges is how a designer picks the mirror of
// what they wanted.
constexpr const char* kTileShapeNames[] = {
    "None",                                    // 0  kNone
    "Full Block",                              // 1  kFullBlock
    "Half Block Bottom",                       // 2  kHalfBlockBottom
    "Half Block Top",                          // 3  kHalfBlockTop
    "Half Block Left",                         // 4  kHalfBlockLeft
    "Half Block Right",                        // 5  kHalfBlockRight
    "45 floor, up right",                      // 6  kSlope45FloorTallRight
    "45 floor, up left",                       // 7  kSlope45FloorTallLeft
    "45 ceiling, down right",                  // 8  kSlope45CeilingTallRight
    "45 ceiling, down left",                   // 9  kSlope45CeilingTallLeft
    "Gentle floor, up right, lower half",      // 10 kGentleSlopeFloorTallRightLower
    "Gentle floor, up right, upper half",      // 11 kGentleSlopeFloorTallRightUpper
    "Gentle floor, up left, lower half",       // 12 kGentleSlopeFloorTallLeftLower
    "Gentle floor, up left, upper half",       // 13 kGentleSlopeFloorTallLeftUpper
    "Gentle ceiling, down right, lower half",  // 14 kGentleSlopeCeilingTallRightLower
    "Gentle ceiling, down right, upper half",  // 15 kGentleSlopeCeilingTallRightUpper
    "Gentle ceiling, down left, lower half",   // 16 kGentleSlopeCeilingTallLeftLower
    "Gentle ceiling, down left, upper half",   // 17 kGentleSlopeCeilingTallLeftUpper
    "Steep floor, up right, bottom cell",      // 18 kSteepSlopeFloorTallRightBottom
    "Steep floor, up right, top cell",         // 19 kSteepSlopeFloorTallRightTop
    "Steep floor, up left, bottom cell",       // 20 kSteepSlopeFloorTallLeftBottom
    "Steep floor, up left, top cell",          // 21 kSteepSlopeFloorTallLeftTop
    "Steep ceiling, down right, bottom cell",  // 22 kSteepSlopeCeilingTallRightBottom
    "Steep ceiling, down right, top cell",     // 23 kSteepSlopeCeilingTallRightTop
    "Steep ceiling, down left, bottom cell",   // 24 kSteepSlopeCeilingTallLeftBottom
    "Steep ceiling, down left, top cell",      // 25 kSteepSlopeCeilingTallLeftTop
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
  const char* shape_preview = (shape_index >= 0 && shape_index < kTileShapeCount)
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
