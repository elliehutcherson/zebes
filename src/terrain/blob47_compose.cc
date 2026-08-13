#include "terrain/blob47_compose.h"

#include <algorithm>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

// Top-left offset of a quadrant within its composed tile, in quadrant units.
struct QuadrantOffset {
  int x = 0;
  int y = 0;
};

QuadrantOffset OffsetForQuadrant(Quadrant quadrant) {
  switch (quadrant) {
    case Quadrant::kNorthWest:
      return {.x = 0, .y = 0};
    case Quadrant::kNorthEast:
      return {.x = 1, .y = 0};
    case Quadrant::kSouthEast:
      return {.x = 1, .y = 1};
    case Quadrant::kSouthWest:
      return {.x = 0, .y = 1};
  }
  return {.x = 0, .y = 0};
}

// Returns true when every pixel in the cell is fully transparent, which marks a
// variant cell as "unchanged, reuse variant 0".
bool IsCellTransparent(const RgbaImage& image, int origin_x, int origin_y, int size) {
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const size_t pixel = (static_cast<size_t>(origin_y + y) * image.width + origin_x + x) * 4;
      if (image.pixels[pixel + 3] != 0) return false;
    }
  }
  return true;
}

// Copies a square region between two tightly packed RGBA images.
void BlitCell(const RgbaImage& source, int source_x, int source_y, RgbaImage& target, int target_x,
              int target_y, int size) {
  for (int y = 0; y < size; ++y) {
    const size_t source_row = (static_cast<size_t>(source_y + y) * source.width + source_x) * 4;
    const size_t target_row = (static_cast<size_t>(target_y + y) * target.width + target_x) * 4;
    std::copy(source.pixels.begin() + source_row,
              source.pixels.begin() + source_row + static_cast<size_t>(size) * 4,
              target.pixels.begin() + target_row);
  }
}

// Resolves which variant actually supplies a quadrant cell, honouring the
// transparent-means-inherit rule.
int ResolveVariantForCell(const QuadrantSheet& sheet, Quadrant quadrant, QuadrantState state,
                          int variant) {
  if (variant == 0) return 0;

  const int column = variant * kQuadrantStateCount + static_cast<int>(state);
  const int origin_x = column * sheet.quadrant_size;
  const int origin_y = static_cast<int>(quadrant) * sheet.quadrant_size;
  if (IsCellTransparent(sheet.image, origin_x, origin_y, sheet.quadrant_size)) return 0;
  return variant;
}

absl::Status ValidateSheet(const QuadrantSheet& sheet) {
  if (!sheet.image.IsValid()) {
    return absl::InvalidArgumentError("quadrant sheet image is malformed");
  }
  if (sheet.quadrant_size <= 0) {
    return absl::InvalidArgumentError("quadrant size must be positive");
  }
  if (sheet.variant_count <= 0) {
    return absl::InvalidArgumentError("quadrant sheet must define at least one variant");
  }

  const int expected_width = sheet.quadrant_size * kQuadrantStateCount * sheet.variant_count;
  const int expected_height = sheet.quadrant_size * kQuadrantCount;
  if (sheet.image.width != expected_width || sheet.image.height != expected_height) {
    return absl::InvalidArgumentError(absl::StrCat(
        "quadrant sheet must be ", expected_width, "x", expected_height, " for ",
        sheet.variant_count, " variant(s) at ", sheet.quadrant_size, "px, got ", sheet.image.width,
        "x", sheet.image.height));
  }
  return absl::OkStatus();
}

// Every quadrant state must be authored in variant 0; there is no fallback.
absl::Status ValidateBaseVariant(const QuadrantSheet& sheet) {
  for (int q = 0; q < kQuadrantCount; ++q) {
    for (int s = 0; s < kQuadrantStateCount; ++s) {
      const int origin_x = s * sheet.quadrant_size;
      const int origin_y = q * sheet.quadrant_size;
      if (!IsCellTransparent(sheet.image, origin_x, origin_y, sheet.quadrant_size)) continue;
      return absl::InvalidArgumentError(
          absl::StrCat("quadrant sheet is missing base artwork for quadrant ", q, " state ", s));
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateSlopeSheet(const SlopeSheet& slopes, int tile_size) {
  if (!slopes.image.IsValid()) {
    return absl::InvalidArgumentError("slope sheet image is malformed");
  }
  if (slopes.tile_size != tile_size) {
    return absl::InvalidArgumentError(absl::StrCat("slope sheet tile size ", slopes.tile_size,
                                                   " does not match composed tile size ",
                                                   tile_size));
  }

  const int expected_width = tile_size * kSlopeShapeCount;
  if (slopes.image.width != expected_width || slopes.image.height != tile_size) {
    return absl::InvalidArgumentError(absl::StrCat("slope sheet must be ", expected_width, "x",
                                                   tile_size, ", got ", slopes.image.width, "x",
                                                   slopes.image.height));
  }
  return absl::OkStatus();
}

// Returns the sheet columns that actually hold artwork. A transparent column
// simply means that slope variant has not been drawn yet.
std::vector<int> FindProvidedSlopeColumns(const SlopeSheet& slopes) {
  std::vector<int> columns;
  for (int column = 0; column < kSlopeShapeCount; ++column) {
    if (IsCellTransparent(slopes.image, column * slopes.tile_size, 0, slopes.tile_size)) continue;
    columns.push_back(column);
  }
  return columns;
}

// Composites the four quadrants of one tile into the atlas.
void ComposeTile(const QuadrantSheet& sheet, uint8_t mask, int variant, int target_x, int target_y,
                 RgbaImage& atlas) {
  for (int q = 0; q < kQuadrantCount; ++q) {
    const Quadrant quadrant = static_cast<Quadrant>(q);
    const QuadrantState state = QuadrantStateForMask(mask, quadrant);
    const int source_variant = ResolveVariantForCell(sheet, quadrant, state, variant);

    const int column = source_variant * kQuadrantStateCount + static_cast<int>(state);
    const QuadrantOffset offset = OffsetForQuadrant(quadrant);
    BlitCell(sheet.image, column * sheet.quadrant_size, q * sheet.quadrant_size, atlas,
             target_x + offset.x * sheet.quadrant_size,
             target_y + offset.y * sheet.quadrant_size, sheet.quadrant_size);
  }
}

}  // namespace

absl::StatusOr<Blob47Atlas> ComposeBlob47(const QuadrantSheet& sheet, const SlopeSheet* slopes) {
  RETURN_IF_ERROR(ValidateSheet(sheet));
  RETURN_IF_ERROR(ValidateBaseVariant(sheet));

  const int tile_size = sheet.quadrant_size * 2;
  absl::Span<const uint8_t> masks = Blob47MaskTable();

  // Provided slope columns are counted first so the atlas can be sized once.
  std::vector<int> slope_columns;
  if (slopes != nullptr) {
    RETURN_IF_ERROR(ValidateSlopeSheet(*slopes, tile_size));
    slope_columns = FindProvidedSlopeColumns(*slopes);
  }
  const int slope_rows =
      (static_cast<int>(slope_columns.size()) + kBlob47Columns - 1) / kBlob47Columns;

  Blob47Atlas atlas;
  atlas.tile_size = tile_size;
  atlas.image.width = kBlob47Columns * tile_size;
  atlas.image.height = (kBlob47Rows * sheet.variant_count + slope_rows) * tile_size;
  atlas.image.pixels.assign(
      static_cast<size_t>(atlas.image.width) * atlas.image.height * 4, 0);
  atlas.tiles.reserve(static_cast<size_t>(masks.size()) * sheet.variant_count);

  for (int variant = 0; variant < sheet.variant_count; ++variant) {
    for (int index = 0; index < static_cast<int>(masks.size()); ++index) {
      const int column = index % kBlob47Columns;
      const int row = variant * kBlob47Rows + index / kBlob47Columns;
      const int target_x = column * tile_size;
      const int target_y = row * tile_size;

      ComposeTile(sheet, masks[index], variant, target_x, target_y, atlas.image);
      atlas.tiles.push_back(ComposedTile{
          .index = index,
          .mask = masks[index],
          .variant = variant,
          .source_x = target_x,
          .source_y = target_y,
      });
    }
  }

  const int slope_origin_row = kBlob47Rows * sheet.variant_count;
  for (int i = 0; i < static_cast<int>(slope_columns.size()); ++i) {
    const int target_x = (i % kBlob47Columns) * tile_size;
    const int target_y = (slope_origin_row + i / kBlob47Columns) * tile_size;

    BlitCell(slopes->image, slope_columns[i] * tile_size, 0, atlas.image, target_x, target_y,
             tile_size);
    atlas.slopes.push_back(ComposedSlope{
        .shape = static_cast<TileShape>(kFirstSlopeShape + slope_columns[i]),
        .source_x = target_x,
        .source_y = target_y,
    });
  }

  return atlas;
}

std::string WriteBlob47Manifest(const Blob47Atlas& atlas) {
  nlohmann::json json;
  json["scheme"] = "blob47";
  json["tile_size"] = atlas.tile_size;
  json["variant_period"] = atlas.variant_period;
  json["atlas_width"] = atlas.image.width;
  json["atlas_height"] = atlas.image.height;

  nlohmann::json tiles = nlohmann::json::array();
  for (const ComposedTile& tile : atlas.tiles) {
    nlohmann::json entry;
    entry["index"] = tile.index;
    entry["mask"] = static_cast<int>(tile.mask);
    entry["variant"] = tile.variant;
    entry["source_x"] = tile.source_x;
    entry["source_y"] = tile.source_y;
    tiles.push_back(std::move(entry));
  }
  json["tiles"] = std::move(tiles);

  // Omitted when no slope units were supplied, so manifests stay minimal.
  if (!atlas.slopes.empty()) {
    nlohmann::json slopes = nlohmann::json::array();
    for (const ComposedSlope& slope : atlas.slopes) {
      nlohmann::json entry;
      entry["shape"] = static_cast<int>(slope.shape);
      entry["source_x"] = slope.source_x;
      entry["source_y"] = slope.source_y;
      slopes.push_back(std::move(entry));
    }
    json["slopes"] = std::move(slopes);
  }

  return json.dump(4);
}

absl::StatusOr<QuadrantSheet> SeedQuadrantSheetFrom3x3(const RgbaImage& source, int tile_size,
                                                       int origin_tile_x, int origin_tile_y,
                                                       InnerCornerSeed inner_corners) {
  if (!source.IsValid()) {
    return absl::InvalidArgumentError("source atlas image is malformed");
  }
  if (tile_size <= 0 || tile_size % 2 != 0) {
    return absl::InvalidArgumentError("tile size must be positive and even to split into quadrants");
  }
  if (origin_tile_x < 0 || origin_tile_y < 0) {
    return absl::InvalidArgumentError("3x3 block origin must be non-negative");
  }

  const int block_x = origin_tile_x * tile_size;
  const int block_y = origin_tile_y * tile_size;
  if (block_x + 3 * tile_size > source.width || block_y + 3 * tile_size > source.height) {
    return absl::OutOfRangeError("3x3 block extends past the source atlas");
  }

  const int quadrant_size = tile_size / 2;
  QuadrantSheet sheet;
  sheet.quadrant_size = quadrant_size;
  sheet.variant_count = 1;
  sheet.image.width = quadrant_size * kQuadrantStateCount;
  sheet.image.height = quadrant_size * kQuadrantCount;
  sheet.image.pixels.assign(
      static_cast<size_t>(sheet.image.width) * sheet.image.height * 4, 0);

  for (int q = 0; q < kQuadrantCount; ++q) {
    const Quadrant quadrant = static_cast<Quadrant>(q);
    const QuadrantOffset offset = OffsetForQuadrant(quadrant);

    for (int s = 0; s < kQuadrantStateCount; ++s) {
      const QuadrantState state = static_cast<QuadrantState>(s);
      // The concave corner is exactly what a 3x3 block lacks. Either leave it
      // for the artist or stand it in with the block's interior.
      const bool is_inner_corner = state == QuadrantState::kInnerCorner;
      if (is_inner_corner && inner_corners == InnerCornerSeed::kLeaveBlank) continue;

      // A covered side is sampled from the block's middle row/column, an
      // exposed side from whichever extreme faces outward for this quadrant.
      // The inner-corner placeholder samples the interior, like kFill.
      const bool vertical_covered = is_inner_corner ||
                                    state == QuadrantState::kEdgeVertical ||
                                    state == QuadrantState::kFill;
      const bool horizontal_covered = is_inner_corner ||
                                      state == QuadrantState::kEdgeHorizontal ||
                                      state == QuadrantState::kFill;
      const int cell_column = horizontal_covered ? 1 : (offset.x == 0 ? 0 : 2);
      const int cell_row = vertical_covered ? 1 : (offset.y == 0 ? 0 : 2);

      BlitCell(source, block_x + cell_column * tile_size + offset.x * quadrant_size,
               block_y + cell_row * tile_size + offset.y * quadrant_size, sheet.image,
               s * quadrant_size, q * quadrant_size, quadrant_size);
    }
  }

  return sheet;
}

absl::Status CopyTile(const RgbaImage& source, int source_x, int source_y, int size,
                      RgbaImage& target, int target_x, int target_y) {
  if (!source.IsValid() || !target.IsValid()) {
    return absl::InvalidArgumentError("copy source and target images must both be well formed");
  }
  if (size <= 0) {
    return absl::InvalidArgumentError("copy size must be positive");
  }
  if (source_x < 0 || source_y < 0 || source_x + size > source.width ||
      source_y + size > source.height) {
    return absl::OutOfRangeError("copy source region falls outside the source image");
  }
  if (target_x < 0 || target_y < 0 || target_x + size > target.width ||
      target_y + size > target.height) {
    return absl::OutOfRangeError("copy target region falls outside the target image");
  }

  BlitCell(source, source_x, source_y, target, target_x, target_y, size);
  return absl::OkStatus();
}

absl::Status SeedInnerCornersFromRing(const RgbaImage& source, int origin_tile_x, int origin_tile_y,
                                      QuadrantSheet& sheet) {
  if (!source.IsValid()) {
    return absl::InvalidArgumentError("source atlas image is malformed");
  }
  if (!sheet.image.IsValid() || sheet.quadrant_size <= 0) {
    return absl::InvalidArgumentError("quadrant sheet must be seeded before its corners are filled");
  }
  if (origin_tile_x < 0 || origin_tile_y < 0) {
    return absl::InvalidArgumentError("3x3 ring origin must be non-negative");
  }

  const int quadrant_size = sheet.quadrant_size;
  const int tile_size = quadrant_size * 2;
  const int ring_x = origin_tile_x * tile_size;
  const int ring_y = origin_tile_y * tile_size;
  if (ring_x + 3 * tile_size > source.width || ring_y + 3 * tile_size > source.height) {
    return absl::OutOfRangeError("3x3 ring extends past the source atlas");
  }

  // A ring is only a ring if the hole is empty and the wall is drawn. Checking
  // both turns a mistyped cell into an error here rather than four wrong
  // corners that only look wrong once painted.
  if (!IsCellTransparent(source, ring_x + tile_size, ring_y + tile_size, tile_size)) {
    return absl::InvalidArgumentError(
        absl::StrCat("ring at tile (", origin_tile_x, ", ", origin_tile_y,
                     ") has a filled centre cell; the centre must be fully transparent"));
  }
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      if (row == 1 && column == 1) continue;
      if (!IsCellTransparent(source, ring_x + column * tile_size, ring_y + row * tile_size,
                             tile_size)) {
        continue;
      }
      return absl::InvalidArgumentError(
          absl::StrCat("ring at tile (", origin_tile_x, ", ", origin_tile_y, ") has an empty cell at (",
                       column, ", ", row, "); all eight wall cells must be drawn"));
    }
  }

  // The hole sits at the ring's centre, so the cell holding a given quadrant's
  // concave corner is the one diagonally opposite that quadrant's direction.
  const int state_column = static_cast<int>(QuadrantState::kInnerCorner);
  for (int q = 0; q < kQuadrantCount; ++q) {
    const QuadrantOffset offset = OffsetForQuadrant(static_cast<Quadrant>(q));
    const int cell_column = offset.x == 0 ? 2 : 0;
    const int cell_row = offset.y == 0 ? 2 : 0;

    BlitCell(source, ring_x + cell_column * tile_size + offset.x * quadrant_size,
             ring_y + cell_row * tile_size + offset.y * quadrant_size, sheet.image,
             state_column * quadrant_size, q * quadrant_size, quadrant_size);
  }

  return absl::OkStatus();
}

}  // namespace zebes
