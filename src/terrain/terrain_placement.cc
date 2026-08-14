#include "terrain/terrain_placement.h"

#include "absl/base/no_destructor.h"

namespace zebes {
namespace {

TerrainPlacementUnit SingleCell(std::string name, TileShape shape) {
  return TerrainPlacementUnit{
      .name = std::move(name), .width = 1, .height = 1, .cells = {shape}};
}

// Which half of a two-cell ramp leads follows from the geometry in
// tile_shape_geometry.h: a gentle ramp's Lower half is the end that starts at
// zero height, so it leads when the ramp rises to the right and trails when it
// rises to the left. Steep units always stack Top over Bottom.
TerrainPlacementUnit SideBySide(std::string name, TileShape left, TileShape right) {
  return TerrainPlacementUnit{
      .name = std::move(name), .width = 2, .height = 1, .cells = {left, right}};
}

TerrainPlacementUnit Stacked(std::string name, TileShape top, TileShape bottom) {
  return TerrainPlacementUnit{
      .name = std::move(name), .width = 1, .height = 2, .cells = {top, bottom}};
}

// Names describe what the player walks on, because that is what the author is
// choosing. The enumerators name the end a wedge tapers to, which is a fact
// about the polygon and not about the ramp.
std::vector<TerrainPlacementUnit> BuildCatalogue() {
  std::vector<TerrainPlacementUnit> units;

  units.push_back(SingleCell("Block", TileShape::kFullBlock));

  units.push_back(SingleCell("Half block, floor", TileShape::kHalfBlockBottom));
  units.push_back(SingleCell("Half block, ceiling", TileShape::kHalfBlockTop));
  units.push_back(SingleCell("Half block, left", TileShape::kHalfBlockLeft));
  units.push_back(SingleCell("Half block, right", TileShape::kHalfBlockRight));

  units.push_back(SingleCell("45 floor, up to the right", TileShape::kSlope45BottomLeft));
  units.push_back(SingleCell("45 floor, up to the left", TileShape::kSlope45BottomRight));
  units.push_back(SingleCell("45 ceiling, down to the right", TileShape::kSlope45TopLeft));
  units.push_back(SingleCell("45 ceiling, down to the left", TileShape::kSlope45TopRight));

  units.push_back(SideBySide("Gentle floor, up to the right",
                             TileShape::kGentleSlopeBottomLeft_Lower,
                             TileShape::kGentleSlopeBottomLeft_Upper));
  units.push_back(SideBySide("Gentle floor, up to the left",
                             TileShape::kGentleSlopeBottomRight_Upper,
                             TileShape::kGentleSlopeBottomRight_Lower));
  units.push_back(SideBySide("Gentle ceiling, down to the right",
                             TileShape::kGentleSlopeTopLeft_Lower,
                             TileShape::kGentleSlopeTopLeft_Upper));
  units.push_back(SideBySide("Gentle ceiling, down to the left",
                             TileShape::kGentleSlopeTopRight_Upper,
                             TileShape::kGentleSlopeTopRight_Lower));

  units.push_back(Stacked("Steep floor, up to the right", TileShape::kSteepSlopeBottomLeft_Top,
                          TileShape::kSteepSlopeBottomLeft_Bottom));
  units.push_back(Stacked("Steep floor, up to the left", TileShape::kSteepSlopeBottomRight_Top,
                          TileShape::kSteepSlopeBottomRight_Bottom));
  units.push_back(Stacked("Steep ceiling, down to the right", TileShape::kSteepSlopeTopLeft_Top,
                          TileShape::kSteepSlopeTopLeft_Bottom));
  units.push_back(Stacked("Steep ceiling, down to the left", TileShape::kSteepSlopeTopRight_Top,
                          TileShape::kSteepSlopeTopRight_Bottom));

  return units;
}

}  // namespace

TileShape TerrainPlacementUnit::At(int x, int y) const {
  if (x < 0 || y < 0 || x >= width || y >= height) return TileShape::kNone;
  return cells[static_cast<size_t>(y) * width + x];
}

absl::Span<const TerrainPlacementUnit> AllTerrainPlacementUnits() {
  static const absl::NoDestructor<std::vector<TerrainPlacementUnit>> kUnits(BuildCatalogue());
  return *kUnits;
}

std::vector<TerrainPlacementUnit> PlacementUnitsWithin(
    const absl::flat_hash_set<TileShape>& available) {
  std::vector<TerrainPlacementUnit> units;
  for (const TerrainPlacementUnit& unit : AllTerrainPlacementUnits()) {
    bool complete = true;
    for (const TileShape cell : unit.cells) {
      if (!available.contains(cell)) {
        complete = false;
        break;
      }
    }
    // All or nothing: half a ramp is broken collision, so a unit missing one
    // cell's artwork must not be offered at all.
    if (complete) units.push_back(unit);
  }
  return units;
}

}  // namespace zebes
