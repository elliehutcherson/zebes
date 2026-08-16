#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "objects/tileset.h"
#include "terrain/terrain_mask.h"

namespace zebes {

// Slope units occupy the contiguous TileShape range from the first 45° variant
// through the last steep ceiling variant.
inline constexpr int kFirstSlopeShape = static_cast<int>(TileShape::kSlope45FloorTallRight);
inline constexpr int kSlopeShapeCount =
    static_cast<int>(TileShape::kSteepSlopeCeilingTallLeftTop) - kFirstSlopeShape + 1;

// The authored quadrant source. Rows are quadrant positions in Quadrant order;
// columns are QuadrantState order, repeated once per variant.
//
// Only 5 x 4 = 20 cells are needed to generate an entire 47-tile terrain, and
// each extra variant costs only the cells that actually differ.
struct QuadrantSheet {
  RgbaImage image;
  // Edge length of one quadrant cell in pixels. Composed tiles are twice this.
  int quadrant_size = 0;
  int variant_count = 1;
};

// One emitted tile: where it landed in the atlas and what neighbourhood it
// depicts. This is the record the tileset importer consumes.
struct ComposedTile {
  // Position within its variant's kBlob47Columns x kBlob47Rows block.
  int index = 0;
  uint8_t mask = 0;
  int variant = 0;
  int source_x = 0;
  int source_y = 0;
};

// Optional slope units, drawn at full tile size rather than decomposed into
// quadrants: one row, one column per slope TileShape in enum order. The
// diagonal grass band is exactly the detail that does not composite well from
// quadrants, which is why these few tiles are drawn directly.
//
// A fully transparent column means that unit was not drawn and is skipped, so a
// first pass can supply only the two 45° units and add gentle or steep variants
// later without changing any code.
struct SlopeSheet {
  RgbaImage image;
  int tile_size = 0;
};

// A slope tile carried alongside the generated blob set. It is placed by hand
// rather than by the brush, but the importer registers it as a terrain member
// so painted ground flows into it instead of capping off with an edge.
struct ComposedSlope {
  TileShape shape = TileShape::kNone;
  int source_x = 0;
  int source_y = 0;
};

// A composited atlas together with the manifest describing every emitted cell.
struct Blob47Atlas {
  RgbaImage image;
  int tile_size = 0;

  // How many tiles the artwork repeats over; see Terrain::variant_period. Zero
  // means the variants are interchangeable, which is what compositing
  // hand-drawn quadrants produces: a variant there is a different drawing of
  // the same cell, not a phase of a larger pattern.
  int variant_period = 0;

  std::vector<ComposedTile> tiles;

  // Appended below the blob blocks. Empty when no slope sheet was supplied.
  std::vector<ComposedSlope> slopes;
};

// Composites all kBlob47TileCount masks for every variant in the sheet.
//
// A quadrant cell that is fully transparent in variant N falls back to the same
// cell in variant 0, so authoring extra variety means drawing only the fill
// quadrants that change. A fully transparent cell in variant 0 is an error:
// there is nothing to fall back to.
// Slope units, when supplied, are appended below the blob blocks and must use
// the same tile size the quadrant sheet implies.
absl::StatusOr<Blob47Atlas> ComposeBlob47(const QuadrantSheet& sheet,
                                          const SlopeSheet* slopes = nullptr);

// Serializes an atlas manifest for the tileset importer.
std::string WriteBlob47Manifest(const Blob47Atlas& atlas);

// Copies one square tile between tightly packed RGBA images, failing when
// either region falls outside its image. Asset tools use this to assemble the
// sheets this header consumes.
absl::Status CopyTile(const RgbaImage& source, int source_x, int source_y, int size,
                      RgbaImage& target, int target_x, int target_y);

// How to treat the four concave-corner cells a 3x3 block cannot supply.
enum class InnerCornerSeed : uint8_t {
  // Leave them transparent. ComposeBlob47 then refuses the sheet, which is the
  // right default: shipping art should not silently contain wrong corners.
  kLeaveBlank,
  // Stand in with the fill quadrant. The terrain tiles correctly and is fully
  // paintable, but concave corners render square instead of rounded. Useful for
  // exercising a terrain before its corner art is drawn.
  kPlaceholderFromFill,
};

// Extracts the 16 quadrants derivable from an existing 3x3 terrain block.
//
// A 3x3 block never contains a concave corner, so those four cells cannot be
// derived from one; every other quadrant state appears somewhere in the block.
// origin_tile_x/y locate the block's top-left cell in atlas tile coordinates.
absl::StatusOr<QuadrantSheet> SeedQuadrantSheetFrom3x3(
    const RgbaImage& atlas, int tile_size, int origin_tile_x, int origin_tile_y,
    InnerCornerSeed inner_corners = InnerCornerSeed::kLeaveBlank);

// Fills a seeded sheet's four concave-corner cells from a 3x3 ring: a solid
// block whose centre cell is fully transparent.
//
// A ring is the smallest shape that contains all four concave corners, one at
// each corner of its hole. The corner facing the hole from the south-east cell
// has terrain to its north and west but nothing diagonally north-west, which is
// exactly the inner-corner condition, and the other three follow by symmetry.
//
// Pairing this with SeedQuadrantSheetFrom3x3 completes all 20 quadrants from a
// single atlas, so no sheet ever has to be assembled by hand.
// origin_tile_x/y locate the ring's top-left cell in atlas tile coordinates.
absl::Status SeedInnerCornersFromRing(const RgbaImage& atlas, int origin_tile_x, int origin_tile_y,
                                      QuadrantSheet& sheet);

}  // namespace zebes
