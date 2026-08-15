#include "terrain/terrain_placement.h"

#include "absl/base/no_destructor.h"

namespace zebes {
namespace {

// Names describe the surface, because that is what an author is choosing. The
// enumerators name the end a wedge tapers to, which is a fact about the polygon
// and not about the ramp.
std::vector<TerrainShapeChoice> BuildCatalogue() {
  return {
      {TileShape::kFullBlock, "Block"},

      {TileShape::kHalfBlockBottom, "Half block, floor"},
      {TileShape::kHalfBlockTop, "Half block, ceiling"},
      {TileShape::kHalfBlockLeft, "Half block, left"},
      {TileShape::kHalfBlockRight, "Half block, right"},

      {TileShape::kSlope45BottomLeft, "45 floor, up to the right"},
      {TileShape::kSlope45BottomRight, "45 floor, up to the left"},
      {TileShape::kSlope45TopLeft, "45 ceiling, down to the right"},
      {TileShape::kSlope45TopRight, "45 ceiling, down to the left"},

      {TileShape::kGentleSlopeBottomLeft_Lower, "Gentle floor, up to the right, lower half"},
      {TileShape::kGentleSlopeBottomLeft_Upper, "Gentle floor, up to the right, upper half"},
      {TileShape::kGentleSlopeBottomRight_Lower, "Gentle floor, up to the left, lower half"},
      {TileShape::kGentleSlopeBottomRight_Upper, "Gentle floor, up to the left, upper half"},
      {TileShape::kGentleSlopeTopLeft_Lower, "Gentle ceiling, down to the right, lower half"},
      {TileShape::kGentleSlopeTopLeft_Upper, "Gentle ceiling, down to the right, upper half"},
      {TileShape::kGentleSlopeTopRight_Lower, "Gentle ceiling, down to the left, lower half"},
      {TileShape::kGentleSlopeTopRight_Upper, "Gentle ceiling, down to the left, upper half"},

      {TileShape::kSteepSlopeBottomLeft_Bottom, "Steep floor, up to the right, bottom cell"},
      {TileShape::kSteepSlopeBottomLeft_Top, "Steep floor, up to the right, top cell"},
      {TileShape::kSteepSlopeBottomRight_Bottom, "Steep floor, up to the left, bottom cell"},
      {TileShape::kSteepSlopeBottomRight_Top, "Steep floor, up to the left, top cell"},
      {TileShape::kSteepSlopeTopLeft_Bottom, "Steep ceiling, down to the right, bottom cell"},
      {TileShape::kSteepSlopeTopLeft_Top, "Steep ceiling, down to the right, top cell"},
      {TileShape::kSteepSlopeTopRight_Bottom, "Steep ceiling, down to the left, bottom cell"},
      {TileShape::kSteepSlopeTopRight_Top, "Steep ceiling, down to the left, top cell"},
  };
}

}  // namespace

absl::Span<const TerrainShapeChoice> AllTerrainShapeChoices() {
  static const absl::NoDestructor<std::vector<TerrainShapeChoice>> kChoices(BuildCatalogue());
  return *kChoices;
}

std::vector<TerrainShapeChoice> ShapeChoicesWithin(
    const absl::flat_hash_set<TileShape>& available) {
  std::vector<TerrainShapeChoice> choices;
  for (const TerrainShapeChoice& choice : AllTerrainShapeChoices()) {
    if (available.contains(choice.shape)) choices.push_back(choice);
  }
  return choices;
}

}  // namespace zebes
