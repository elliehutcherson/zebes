#include "terrain/terrain_placement.h"

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"

namespace zebes {
namespace {

// Names describe the surface, because that is what an author is choosing. The
// enumerators name the end a wedge tapers to, which is a fact about the polygon
// and not about the ramp.
// The fully surrounded mask. Its tile is the interior of a filled region.
constexpr uint8_t kSolidMask = 255;

// Whether a key depicts a full block with its own material on all eight sides.
bool IsSurroundedInterior(const TerrainCellKey& key) {
  if (key.shape != TileShape::kFullBlock) return false;
  for (const TileShape neighbor : key.neighbors) {
    if (neighbor != TileShape::kFullBlock) return false;
  }
  return true;
}

// Group labels. Named here rather than spelled at each row so the palette and
// any other reader agree about which family a piece belongs to.
constexpr char kBlockGroup[] = "Block";
constexpr char kHalfBlockGroup[] = "Half blocks";
constexpr char kSlope45Group[] = "45 slopes";
constexpr char kGentleGroup[] = "Gentle slopes";
constexpr char kSteepGroup[] = "Steep slopes";

std::vector<TerrainShapeChoice> BuildCatalogue() {
  return {
      {TileShape::kFullBlock, "Block", kBlockGroup},

      {TileShape::kHalfBlockBottom, "Half block, floor", kHalfBlockGroup},
      {TileShape::kHalfBlockTop, "Half block, ceiling", kHalfBlockGroup},
      {TileShape::kHalfBlockLeft, "Half block, left", kHalfBlockGroup},
      {TileShape::kHalfBlockRight, "Half block, right", kHalfBlockGroup},

      {TileShape::kSlope45FloorTallRight, "45 floor, up to the right", kSlope45Group},
      {TileShape::kSlope45FloorTallLeft, "45 floor, up to the left", kSlope45Group},
      {TileShape::kSlope45CeilingTallRight, "45 ceiling, down to the right", kSlope45Group},
      {TileShape::kSlope45CeilingTallLeft, "45 ceiling, down to the left", kSlope45Group},

      {TileShape::kGentleSlopeFloorTallRightLower, "Gentle floor, up to the right, lower half",
       kGentleGroup},
      {TileShape::kGentleSlopeFloorTallRightUpper, "Gentle floor, up to the right, upper half",
       kGentleGroup},
      {TileShape::kGentleSlopeFloorTallLeftLower, "Gentle floor, up to the left, lower half",
       kGentleGroup},
      {TileShape::kGentleSlopeFloorTallLeftUpper, "Gentle floor, up to the left, upper half",
       kGentleGroup},
      {TileShape::kGentleSlopeCeilingTallRightLower,
       "Gentle ceiling, down to the right, lower half", kGentleGroup},
      {TileShape::kGentleSlopeCeilingTallRightUpper,
       "Gentle ceiling, down to the right, upper half", kGentleGroup},
      {TileShape::kGentleSlopeCeilingTallLeftLower, "Gentle ceiling, down to the left, lower half",
       kGentleGroup},
      {TileShape::kGentleSlopeCeilingTallLeftUpper, "Gentle ceiling, down to the left, upper half",
       kGentleGroup},

      {TileShape::kSteepSlopeFloorTallRightBottom, "Steep floor, up to the right, bottom cell",
       kSteepGroup},
      {TileShape::kSteepSlopeFloorTallRightTop, "Steep floor, up to the right, top cell",
       kSteepGroup},
      {TileShape::kSteepSlopeFloorTallLeftBottom, "Steep floor, up to the left, bottom cell",
       kSteepGroup},
      {TileShape::kSteepSlopeFloorTallLeftTop, "Steep floor, up to the left, top cell",
       kSteepGroup},
      {TileShape::kSteepSlopeCeilingTallRightBottom,
       "Steep ceiling, down to the right, bottom cell", kSteepGroup},
      {TileShape::kSteepSlopeCeilingTallRightTop, "Steep ceiling, down to the right, top cell",
       kSteepGroup},
      {TileShape::kSteepSlopeCeilingTallLeftBottom, "Steep ceiling, down to the left, bottom cell",
       kSteepGroup},
      {TileShape::kSteepSlopeCeilingTallLeftTop, "Steep ceiling, down to the left, top cell",
       kSteepGroup},
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

absl::flat_hash_set<TileShape> PaintableShapesOf(const Terrain& terrain, const Tileset& tileset) {
  absl::flat_hash_set<TileShape> shapes;

  if (terrain.scheme == TerrainScheme::kDerived) {
    for (int i = 1; i <= static_cast<int>(TileShape::kSteepSlopeCeilingTallLeftTop); ++i) {
      shapes.insert(static_cast<TileShape>(i));
    }
    return shapes;
  }

  absl::flat_hash_map<int, TileShape> shape_by_tile;
  for (const Tile& tile : tileset.tiles) shape_by_tile.emplace(tile.id, tile.shape);

  const auto claim = [&](int tile_id) {
    auto found = shape_by_tile.find(tile_id);
    if (found != shape_by_tile.end() && found->second != TileShape::kNone) {
      shapes.insert(found->second);
    }
  };

  for (const TerrainRule& rule : terrain.rules) {
    for (const TerrainVariant& variant : rule.variants) claim(variant.tile_id);
  }
  for (const int tile_id : terrain.shape_tile_ids) claim(tile_id);
  return shapes;
}

const Tile* TerrainSwatchTile(const Tileset& tileset, const Terrain& terrain) {
  const auto tile_by_id = [&tileset](int tile_id) -> const Tile* {
    for (const Tile& tile : tileset.tiles) {
      if (tile.id == tile_id) return &tile;
    }
    return nullptr;
  };

  if (terrain.scheme == TerrainScheme::kDerived) {
    for (const DerivedTile& derived : terrain.derived_tiles) {
      if (IsSurroundedInterior(derived.key)) return tile_by_id(derived.tile_id);
    }
    // Nothing interior has been painted yet, so any artwork this terrain has is
    // a better answer than a blank.
    if (!terrain.derived_tiles.empty()) return tile_by_id(terrain.derived_tiles.front().tile_id);
    return nullptr;
  }

  for (const TerrainRule& rule : terrain.rules) {
    if (rule.mask != kSolidMask || rule.variants.empty()) continue;
    if (const Tile* tile = tile_by_id(rule.variants.front().tile_id); tile != nullptr) return tile;
  }
  return nullptr;
}

}  // namespace zebes
