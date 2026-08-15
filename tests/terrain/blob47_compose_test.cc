#include "terrain/blob47_compose.h"

#include <set>

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

constexpr int kQuadrantSize = 4;

// Encodes (quadrant, state, variant) into a pixel so a composed tile can be
// decoded back and checked against QuadrantStateForMask.
uint8_t MarkerFor(int quadrant, int state, int variant) {
  return static_cast<uint8_t>(1 + quadrant * kQuadrantStateCount + state + variant * 100);
}

void FillCell(RgbaImage& image, int origin_x, int origin_y, int size, uint8_t marker,
              uint8_t alpha) {
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const size_t pixel = (static_cast<size_t>(origin_y + y) * image.width + origin_x + x) * 4;
      image.pixels[pixel + 0] = marker;
      image.pixels[pixel + 1] = marker;
      image.pixels[pixel + 2] = marker;
      image.pixels[pixel + 3] = alpha;
    }
  }
}

// Builds a sheet whose every cell is a flat marker color. Variants above zero
// are left transparent unless listed in opaque_variant_states.
QuadrantSheet MakeSheet(int variant_count, const std::set<int>& opaque_variant_states = {}) {
  QuadrantSheet sheet;
  sheet.quadrant_size = kQuadrantSize;
  sheet.variant_count = variant_count;
  sheet.image.width = kQuadrantSize * kQuadrantStateCount * variant_count;
  sheet.image.height = kQuadrantSize * kQuadrantCount;
  sheet.image.pixels.assign(
      static_cast<size_t>(sheet.image.width) * sheet.image.height * 4, 0);

  for (int variant = 0; variant < variant_count; ++variant) {
    for (int q = 0; q < kQuadrantCount; ++q) {
      for (int s = 0; s < kQuadrantStateCount; ++s) {
        const bool opaque = variant == 0 || opaque_variant_states.count(s) == 1;
        const int column = variant * kQuadrantStateCount + s;
        FillCell(sheet.image, column * kQuadrantSize, q * kQuadrantSize, kQuadrantSize,
                 MarkerFor(q, s, variant), opaque ? 255 : 0);
      }
    }
  }
  return sheet;
}

// Reads the marker at the centre of one quadrant of a composed tile.
uint8_t ReadQuadrantMarker(const RgbaImage& image, int tile_x, int tile_y, Quadrant quadrant) {
  const int offset_x = (quadrant == Quadrant::kNorthEast || quadrant == Quadrant::kSouthEast)
                           ? kQuadrantSize
                           : 0;
  const int offset_y = (quadrant == Quadrant::kSouthEast || quadrant == Quadrant::kSouthWest)
                           ? kQuadrantSize
                           : 0;
  const int x = tile_x + offset_x + kQuadrantSize / 2;
  const int y = tile_y + offset_y + kQuadrantSize / 2;
  return image.pixels[(static_cast<size_t>(y) * image.width + x) * 4];
}

TEST(Blob47ComposeTest, EmitsEveryMaskOnce) {
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1));
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  EXPECT_EQ(atlas->tile_size, kQuadrantSize * 2);
  ASSERT_EQ(atlas->tiles.size(), kBlob47TileCount);

  std::set<uint8_t> masks;
  for (const ComposedTile& tile : atlas->tiles) masks.insert(tile.mask);
  EXPECT_EQ(masks.size(), kBlob47TileCount);
}

TEST(Blob47ComposeTest, AtlasDimensionsFollowTheBlobGrid) {
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1));
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  EXPECT_EQ(atlas->image.width, kBlob47Columns * kQuadrantSize * 2);
  EXPECT_EQ(atlas->image.height, kBlob47Rows * kQuadrantSize * 2);
  EXPECT_TRUE(atlas->image.IsValid());
}

// The core guarantee: every composed quadrant is the artwork the runtime mask
// table says it should be. If this drifts, painted levels render wrong art.
TEST(Blob47ComposeTest, EveryQuadrantMatchesQuadrantStateForMask) {
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1));
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  for (const ComposedTile& tile : atlas->tiles) {
    for (int q = 0; q < kQuadrantCount; ++q) {
      const Quadrant quadrant = static_cast<Quadrant>(q);
      const QuadrantState expected = QuadrantStateForMask(tile.mask, quadrant);
      const uint8_t marker =
          ReadQuadrantMarker(atlas->image, tile.source_x, tile.source_y, quadrant);
      EXPECT_EQ(marker, MarkerFor(q, static_cast<int>(expected), 0))
          << "mask " << static_cast<int>(tile.mask) << " quadrant " << q;
    }
  }
}

TEST(Blob47ComposeTest, FullySurroundedTileIsAllFillQuadrants) {
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1));
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  const ComposedTile* solid = nullptr;
  for (const ComposedTile& tile : atlas->tiles) {
    if (tile.mask == 255) solid = &tile;
  }
  ASSERT_NE(solid, nullptr);

  for (int q = 0; q < kQuadrantCount; ++q) {
    EXPECT_EQ(ReadQuadrantMarker(atlas->image, solid->source_x, solid->source_y,
                                 static_cast<Quadrant>(q)),
              MarkerFor(q, static_cast<int>(QuadrantState::kFill), 0));
  }
}

TEST(Blob47ComposeTest, VariantsEmitSeparateBlocks) {
  absl::StatusOr<Blob47Atlas> atlas =
      ComposeBlob47(MakeSheet(3, {static_cast<int>(QuadrantState::kFill)}));
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  EXPECT_EQ(atlas->tiles.size(), static_cast<size_t>(kBlob47TileCount) * 3);
  EXPECT_EQ(atlas->image.height, kBlob47Rows * kQuadrantSize * 2 * 3);

  // Variant blocks stack vertically, so the same index moves down a full block.
  const ComposedTile& first = atlas->tiles[0];
  const ComposedTile& second = atlas->tiles[kBlob47TileCount];
  EXPECT_EQ(first.index, second.index);
  EXPECT_EQ(first.mask, second.mask);
  EXPECT_EQ(second.variant, 1);
  EXPECT_EQ(second.source_x, first.source_x);
  EXPECT_EQ(second.source_y, first.source_y + kBlob47Rows * kQuadrantSize * 2);
}

// Authoring variety should cost only the cells that differ.
TEST(Blob47ComposeTest, TransparentVariantCellsInheritFromBaseVariant) {
  absl::StatusOr<Blob47Atlas> atlas =
      ComposeBlob47(MakeSheet(2, {static_cast<int>(QuadrantState::kFill)}));
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  const ComposedTile* solid = nullptr;
  const ComposedTile* isolated = nullptr;
  for (const ComposedTile& tile : atlas->tiles) {
    if (tile.variant != 1) continue;
    if (tile.mask == 255) solid = &tile;
    if (tile.mask == 0) isolated = &tile;
  }
  ASSERT_NE(solid, nullptr);
  ASSERT_NE(isolated, nullptr);

  // The fill cell was authored for variant 1, so it uses variant 1 artwork.
  EXPECT_EQ(
      ReadQuadrantMarker(atlas->image, solid->source_x, solid->source_y, Quadrant::kNorthWest),
      MarkerFor(0, static_cast<int>(QuadrantState::kFill), 1));

  // The outer corner was left transparent, so it falls back to variant 0.
  EXPECT_EQ(ReadQuadrantMarker(atlas->image, isolated->source_x, isolated->source_y,
                               Quadrant::kNorthWest),
            MarkerFor(0, static_cast<int>(QuadrantState::kOuterCorner), 0));
}

TEST(Blob47ComposeTest, RejectsMissingBaseArtwork) {
  QuadrantSheet sheet = MakeSheet(1);
  // Blank the inner corner of the north-west quadrant.
  FillCell(sheet.image, static_cast<int>(QuadrantState::kInnerCorner) * kQuadrantSize, 0,
           kQuadrantSize, 0, 0);

  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(sheet);
  ASSERT_FALSE(atlas.ok());
  EXPECT_EQ(atlas.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(atlas.status().message().find("state 3"), std::string::npos)
      << atlas.status().message();
}

TEST(Blob47ComposeTest, RejectsWronglySizedSheet) {
  QuadrantSheet sheet = MakeSheet(1);
  sheet.variant_count = 2;  // Image is still sized for one variant.

  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(sheet);
  ASSERT_FALSE(atlas.ok());
  EXPECT_EQ(atlas.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(Blob47ComposeTest, ManifestDescribesEveryEmittedCell) {
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(2));
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  nlohmann::json json = nlohmann::json::parse(WriteBlob47Manifest(*atlas));
  EXPECT_EQ(json["scheme"], "blob47");
  EXPECT_EQ(json["tile_size"], kQuadrantSize * 2);
  ASSERT_EQ(json["tiles"].size(), atlas->tiles.size());

  for (size_t i = 0; i < atlas->tiles.size(); ++i) {
    const ComposedTile& tile = atlas->tiles[i];
    EXPECT_EQ(json["tiles"][i]["index"], tile.index);
    EXPECT_EQ(json["tiles"][i]["mask"], static_cast<int>(tile.mask));
    EXPECT_EQ(json["tiles"][i]["variant"], tile.variant);
    EXPECT_EQ(json["tiles"][i]["source_x"], tile.source_x);
    EXPECT_EQ(json["tiles"][i]["source_y"], tile.source_y);
  }
}

// --- Slope units --------------------------------------------------------------

// A slope sheet with only the listed columns drawn; the rest stay transparent
// to mean "this variant has not been authored yet".
SlopeSheet MakeSlopeSheet(const std::set<int>& provided_columns) {
  const int tile_size = kQuadrantSize * 2;
  SlopeSheet sheet;
  sheet.tile_size = tile_size;
  sheet.image.width = tile_size * kSlopeShapeCount;
  sheet.image.height = tile_size;
  sheet.image.pixels.assign(
      static_cast<size_t>(sheet.image.width) * sheet.image.height * 4, 0);

  for (int column : provided_columns) {
    FillCell(sheet.image, column * tile_size, 0, tile_size,
             static_cast<uint8_t>(200 + column), 255);
  }
  return sheet;
}

TEST(Blob47ComposeTest, SlopeSheetIsOptional) {
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1), nullptr);
  ASSERT_TRUE(atlas.ok()) << atlas.status();
  EXPECT_TRUE(atlas->slopes.empty());
  EXPECT_EQ(atlas->image.height, kBlob47Rows * kQuadrantSize * 2);
}

TEST(Blob47ComposeTest, ProvidedSlopeColumnsAreAppendedBelowTheBlobBlocks) {
  // Columns 0 and 1 are the two 45° floor variants.
  SlopeSheet slopes = MakeSlopeSheet({0, 1});
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1), &slopes);
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  ASSERT_EQ(atlas->slopes.size(), 2u);
  EXPECT_EQ(atlas->slopes[0].shape, TileShape::kSlope45BottomLeft);
  EXPECT_EQ(atlas->slopes[1].shape, TileShape::kSlope45BottomRight);

  // One extra atlas row, immediately below the single blob block.
  const int tile_size = kQuadrantSize * 2;
  EXPECT_EQ(atlas->image.height, (kBlob47Rows + 1) * tile_size);
  EXPECT_EQ(atlas->slopes[0].source_x, 0);
  EXPECT_EQ(atlas->slopes[0].source_y, kBlob47Rows * tile_size);
  EXPECT_EQ(atlas->slopes[1].source_x, tile_size);
}

// Undrawn columns must not consume atlas space or produce phantom tiles.
TEST(Blob47ComposeTest, TransparentSlopeColumnsAreSkipped) {
  SlopeSheet slopes = MakeSlopeSheet({0, 7});
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1), &slopes);
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  ASSERT_EQ(atlas->slopes.size(), 2u);
  EXPECT_EQ(atlas->slopes[0].shape, TileShape::kSlope45BottomLeft);
  // Column 7 is the eighth slope shape, not the eighth atlas cell.
  EXPECT_EQ(static_cast<int>(atlas->slopes[1].shape), kFirstSlopeShape + 7);
  // They pack contiguously regardless of which columns were drawn.
  EXPECT_EQ(atlas->slopes[1].source_x, kQuadrantSize * 2);
}

TEST(Blob47ComposeTest, SlopeArtworkLandsInTheAtlas) {
  SlopeSheet slopes = MakeSlopeSheet({0});
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1), &slopes);
  ASSERT_TRUE(atlas.ok()) << atlas.status();
  ASSERT_EQ(atlas->slopes.size(), 1u);

  const ComposedSlope& slope = atlas->slopes[0];
  const size_t pixel =
      (static_cast<size_t>(slope.source_y) * atlas->image.width + slope.source_x) * 4;
  EXPECT_EQ(atlas->image.pixels[pixel], 200);
  EXPECT_EQ(atlas->image.pixels[pixel + 3], 255);
}

TEST(Blob47ComposeTest, SlopesStackBelowMultipleVariantBlocks) {
  SlopeSheet slopes = MakeSlopeSheet({0});
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(3), &slopes);
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  const int tile_size = kQuadrantSize * 2;
  EXPECT_EQ(atlas->slopes[0].source_y, kBlob47Rows * 3 * tile_size);
  EXPECT_EQ(atlas->image.height, (kBlob47Rows * 3 + 1) * tile_size);
}

TEST(Blob47ComposeTest, RejectsWronglySizedSlopeSheet) {
  SlopeSheet slopes = MakeSlopeSheet({0});
  slopes.image.width -= kQuadrantSize * 2;
  slopes.image.pixels.resize(
      static_cast<size_t>(slopes.image.width) * slopes.image.height * 4);

  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1), &slopes);
  ASSERT_FALSE(atlas.ok());
  EXPECT_EQ(atlas.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(Blob47ComposeTest, RejectsSlopeSheetWithMismatchedTileSize) {
  SlopeSheet slopes = MakeSlopeSheet({0});
  slopes.tile_size = kQuadrantSize;  // Half the composed tile size.

  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1), &slopes);
  ASSERT_FALSE(atlas.ok());
  EXPECT_EQ(atlas.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(Blob47ComposeTest, ManifestCarriesSlopeShapes) {
  SlopeSheet slopes = MakeSlopeSheet({0, 1});
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1), &slopes);
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  nlohmann::json json = nlohmann::json::parse(WriteBlob47Manifest(*atlas));
  ASSERT_EQ(json["slopes"].size(), 2u);
  // Spelled with the stable identifier, so renumbering TileShape cannot silently
  // reinterpret a manifest written today.
  EXPECT_EQ(json["slopes"][0]["shape"], "kSlope45BottomLeft");
  EXPECT_EQ(json["slopes"][0]["source_y"], atlas->slopes[0].source_y);
}

// An absent list and an empty one would be two spellings of one state, leaving
// the reader to guess which the author meant.
TEST(Blob47ComposeTest, ManifestWritesAnEmptySlopesArrayWhenNoneSupplied) {
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1));
  ASSERT_TRUE(atlas.ok()) << atlas.status();

  nlohmann::json json = nlohmann::json::parse(WriteBlob47Manifest(*atlas));
  ASSERT_TRUE(json.contains("slopes"));
  EXPECT_TRUE(json["slopes"].is_array());
  EXPECT_TRUE(json["slopes"].empty());
}

// --- Seeding a sheet from an existing 3x3 block -------------------------------

// Builds an atlas whose 3x3 block encodes each cell's (column, row) position, so
// the seeded sheet can be checked against the expected sampling positions.
RgbaImage Make3x3Atlas(int tile_size, int origin_tile_x, int origin_tile_y) {
  RgbaImage image;
  image.width = (origin_tile_x + 3) * tile_size;
  image.height = (origin_tile_y + 3) * tile_size;
  image.pixels.assign(static_cast<size_t>(image.width) * image.height * 4, 0);

  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      FillCell(image, (origin_tile_x + column) * tile_size, (origin_tile_y + row) * tile_size,
               tile_size, static_cast<uint8_t>(1 + row * 3 + column), 255);
    }
  }
  return image;
}

TEST(Blob47ComposeTest, SeedFrom3x3ExtractsSixteenQuadrantsAndBlanksInnerCorners) {
  constexpr int kTileSize = 8;
  RgbaImage source = Make3x3Atlas(kTileSize, 1, 1);

  absl::StatusOr<QuadrantSheet> sheet = SeedQuadrantSheetFrom3x3(source, kTileSize, 1, 1);
  ASSERT_TRUE(sheet.ok()) << sheet.status();

  EXPECT_EQ(sheet->quadrant_size, kTileSize / 2);
  EXPECT_EQ(sheet->variant_count, 1);

  int opaque_cells = 0;
  int blank_cells = 0;
  for (int q = 0; q < kQuadrantCount; ++q) {
    for (int s = 0; s < kQuadrantStateCount; ++s) {
      const size_t pixel = (static_cast<size_t>(q * sheet->quadrant_size) * sheet->image.width +
                            s * sheet->quadrant_size) * 4;
      if (sheet->image.pixels[pixel + 3] == 0) {
        ++blank_cells;
        EXPECT_EQ(s, static_cast<int>(QuadrantState::kInnerCorner))
            << "unexpected blank at quadrant " << q << " state " << s;
        continue;
      }
      ++opaque_cells;
    }
  }

  EXPECT_EQ(opaque_cells, 16);
  EXPECT_EQ(blank_cells, kQuadrantCount);
}

TEST(Blob47ComposeTest, SeedFrom3x3SamplesTheCorrectBlockCells) {
  constexpr int kTileSize = 8;
  RgbaImage source = Make3x3Atlas(kTileSize, 0, 0);

  absl::StatusOr<QuadrantSheet> sheet = SeedQuadrantSheetFrom3x3(source, kTileSize, 0, 0);
  ASSERT_TRUE(sheet.ok()) << sheet.status();

  const auto marker_at = [&](Quadrant quadrant, QuadrantState state) {
    const size_t pixel =
        (static_cast<size_t>(static_cast<int>(quadrant) * sheet->quadrant_size) *
             sheet->image.width +
         static_cast<int>(state) * sheet->quadrant_size) * 4;
    return sheet->image.pixels[pixel];
  };
  // Marker value is 1 + row * 3 + column for the sampled 3x3 cell.
  const auto cell = [](int column, int row) { return static_cast<uint8_t>(1 + row * 3 + column); };

  // North-west quadrant: exposed sides come from the top-left extremes.
  EXPECT_EQ(marker_at(Quadrant::kNorthWest, QuadrantState::kOuterCorner), cell(0, 0));
  EXPECT_EQ(marker_at(Quadrant::kNorthWest, QuadrantState::kEdgeVertical), cell(0, 1));
  EXPECT_EQ(marker_at(Quadrant::kNorthWest, QuadrantState::kEdgeHorizontal), cell(1, 0));
  EXPECT_EQ(marker_at(Quadrant::kNorthWest, QuadrantState::kFill), cell(1, 1));

  // South-east quadrant mirrors to the bottom-right extremes.
  EXPECT_EQ(marker_at(Quadrant::kSouthEast, QuadrantState::kOuterCorner), cell(2, 2));
  EXPECT_EQ(marker_at(Quadrant::kSouthEast, QuadrantState::kEdgeVertical), cell(2, 1));
  EXPECT_EQ(marker_at(Quadrant::kSouthEast, QuadrantState::kEdgeHorizontal), cell(1, 2));
  EXPECT_EQ(marker_at(Quadrant::kSouthEast, QuadrantState::kFill), cell(1, 1));
}

// A 3x3-seeded sheet is deliberately incomplete, so composing it must fail
// rather than emit square concave corners as if they were finished art.
TEST(Blob47ComposeTest, SeedFrom3x3LeavesSheetUncomposableUntilCornersAreDrawn) {
  constexpr int kTileSize = 8;
  RgbaImage source = Make3x3Atlas(kTileSize, 0, 0);

  absl::StatusOr<QuadrantSheet> sheet = SeedQuadrantSheetFrom3x3(source, kTileSize, 0, 0);
  ASSERT_TRUE(sheet.ok()) << sheet.status();

  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(*sheet);
  ASSERT_FALSE(atlas.ok());
  EXPECT_EQ(atlas.status().code(), absl::StatusCode::kInvalidArgument);
}

// Opting into placeholders makes the terrain paintable before corner art exists.
TEST(Blob47ComposeTest, SeedFrom3x3WithPlaceholdersComposesAndUsesInteriorArt) {
  constexpr int kTileSize = 8;
  RgbaImage source = Make3x3Atlas(kTileSize, 0, 0);

  absl::StatusOr<QuadrantSheet> sheet = SeedQuadrantSheetFrom3x3(
      source, kTileSize, 0, 0, InnerCornerSeed::kPlaceholderFromFill);
  ASSERT_TRUE(sheet.ok()) << sheet.status();

  // The placeholder samples the block's centre cell, same as kFill.
  const size_t inner_pixel =
      static_cast<size_t>(static_cast<int>(QuadrantState::kInnerCorner)) *
      sheet->quadrant_size * 4;
  EXPECT_EQ(sheet->image.pixels[inner_pixel + 3], 255);
  EXPECT_EQ(sheet->image.pixels[inner_pixel], static_cast<uint8_t>(1 + 1 * 3 + 1));

  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(*sheet);
  ASSERT_TRUE(atlas.ok()) << atlas.status();
  EXPECT_EQ(atlas->tiles.size(), kBlob47TileCount);
  EXPECT_EQ(atlas->tile_size, kTileSize);
}

TEST(Blob47ComposeTest, SeedFrom3x3RejectsBlockOutsideTheAtlas) {
  RgbaImage source = Make3x3Atlas(8, 0, 0);

  absl::StatusOr<QuadrantSheet> sheet = SeedQuadrantSheetFrom3x3(source, 8, 2, 0);
  ASSERT_FALSE(sheet.ok());
  EXPECT_EQ(sheet.status().code(), absl::StatusCode::kOutOfRange);
}

TEST(Blob47ComposeTest, SeedFrom3x3RejectsOddTileSize) {
  RgbaImage source = Make3x3Atlas(8, 0, 0);

  absl::StatusOr<QuadrantSheet> sheet = SeedQuadrantSheetFrom3x3(source, 7, 0, 0);
  ASSERT_FALSE(sheet.ok());
  EXPECT_EQ(sheet.status().code(), absl::StatusCode::kInvalidArgument);
}

// --- Seeding concave corners from a 3x3 ring ----------------------------------

// Builds an atlas holding a 3x3 ring: the eight wall cells carry their
// (column, row) marker and the centre is fully transparent.
RgbaImage MakeRingAtlas(int tile_size, int origin_tile_x, int origin_tile_y) {
  RgbaImage image;
  image.width = (origin_tile_x + 3) * tile_size;
  image.height = (origin_tile_y + 3) * tile_size;
  image.pixels.assign(static_cast<size_t>(image.width) * image.height * 4, 0);

  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      if (row == 1 && column == 1) continue;
      FillCell(image, (origin_tile_x + column) * tile_size, (origin_tile_y + row) * tile_size,
               tile_size, static_cast<uint8_t>(1 + row * 3 + column), 255);
    }
  }
  return image;
}

// Combining the two seeders must leave nothing for the artist to fill in.
TEST(Blob47ComposeTest, RingSeedCompletesTheSheetAndItComposes) {
  constexpr int kTileSize = 8;
  RgbaImage block = Make3x3Atlas(kTileSize, 0, 0);
  RgbaImage ring = MakeRingAtlas(kTileSize, 0, 0);

  absl::StatusOr<QuadrantSheet> sheet = SeedQuadrantSheetFrom3x3(block, kTileSize, 0, 0);
  ASSERT_TRUE(sheet.ok()) << sheet.status();
  ASSERT_TRUE(SeedInnerCornersFromRing(ring, 0, 0, *sheet).ok());

  for (int q = 0; q < kQuadrantCount; ++q) {
    for (int s = 0; s < kQuadrantStateCount; ++s) {
      const size_t pixel = (static_cast<size_t>(q * sheet->quadrant_size) * sheet->image.width +
                            s * sheet->quadrant_size) * 4;
      EXPECT_EQ(sheet->image.pixels[pixel + 3], 255)
          << "quadrant " << q << " state " << s << " is still blank";
    }
  }

  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(*sheet);
  ASSERT_TRUE(atlas.ok()) << atlas.status();
  EXPECT_EQ(atlas->tiles.size(), kBlob47TileCount);
}

// Each quadrant's concave corner must come from the ring cell diagonally
// opposite that quadrant's direction, since that is the cell whose corner faces
// the hole.
TEST(Blob47ComposeTest, RingSeedSamplesTheCellFacingTheHole) {
  constexpr int kTileSize = 8;
  RgbaImage block = Make3x3Atlas(kTileSize, 0, 0);
  RgbaImage ring = MakeRingAtlas(kTileSize, 0, 0);

  absl::StatusOr<QuadrantSheet> sheet = SeedQuadrantSheetFrom3x3(block, kTileSize, 0, 0);
  ASSERT_TRUE(sheet.ok()) << sheet.status();
  ASSERT_TRUE(SeedInnerCornersFromRing(ring, 0, 0, *sheet).ok());

  const auto marker_at = [&](Quadrant quadrant) {
    const size_t pixel =
        (static_cast<size_t>(static_cast<int>(quadrant) * sheet->quadrant_size) *
             sheet->image.width +
         static_cast<int>(QuadrantState::kInnerCorner) * sheet->quadrant_size) * 4;
    return sheet->image.pixels[pixel];
  };
  const auto cell = [](int column, int row) { return static_cast<uint8_t>(1 + row * 3 + column); };

  EXPECT_EQ(marker_at(Quadrant::kNorthWest), cell(2, 2));
  EXPECT_EQ(marker_at(Quadrant::kNorthEast), cell(0, 2));
  EXPECT_EQ(marker_at(Quadrant::kSouthEast), cell(0, 0));
  EXPECT_EQ(marker_at(Quadrant::kSouthWest), cell(2, 0));
}

TEST(Blob47ComposeTest, RingSeedRejectsASolidBlock) {
  constexpr int kTileSize = 8;
  RgbaImage block = Make3x3Atlas(kTileSize, 0, 0);

  absl::StatusOr<QuadrantSheet> sheet = SeedQuadrantSheetFrom3x3(block, kTileSize, 0, 0);
  ASSERT_TRUE(sheet.ok()) << sheet.status();

  const absl::Status status = SeedInnerCornersFromRing(block, 0, 0, *sheet);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("centre"), std::string::npos) << status.message();
}

TEST(Blob47ComposeTest, RingSeedRejectsAHollowWall) {
  constexpr int kTileSize = 8;
  RgbaImage block = Make3x3Atlas(kTileSize, 0, 0);
  RgbaImage ring = MakeRingAtlas(kTileSize, 0, 0);
  // Erase one wall cell: the ring is no longer closed, so a corner would be air.
  FillCell(ring, 0, 0, kTileSize, 0, 0);

  absl::StatusOr<QuadrantSheet> sheet = SeedQuadrantSheetFrom3x3(block, kTileSize, 0, 0);
  ASSERT_TRUE(sheet.ok()) << sheet.status();

  const absl::Status status = SeedInnerCornersFromRing(ring, 0, 0, *sheet);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(Blob47ComposeTest, RingSeedRejectsARingOutsideTheAtlas) {
  constexpr int kTileSize = 8;
  RgbaImage block = Make3x3Atlas(kTileSize, 0, 0);
  RgbaImage ring = MakeRingAtlas(kTileSize, 0, 0);

  absl::StatusOr<QuadrantSheet> sheet = SeedQuadrantSheetFrom3x3(block, kTileSize, 0, 0);
  ASSERT_TRUE(sheet.ok()) << sheet.status();

  const absl::Status status = SeedInnerCornersFromRing(ring, 2, 0, *sheet);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kOutOfRange);
}

// --- CopyTile -----------------------------------------------------------------

TEST(Blob47ComposeTest, CopyTileMovesOneTileBetweenImages) {
  constexpr int kTileSize = 8;
  RgbaImage source = Make3x3Atlas(kTileSize, 0, 0);

  RgbaImage target;
  target.width = kTileSize * 2;
  target.height = kTileSize;
  target.pixels.assign(static_cast<size_t>(target.width) * target.height * 4, 0);

  ASSERT_TRUE(CopyTile(source, 2 * kTileSize, kTileSize, kTileSize, target, kTileSize, 0).ok());

  // Source cell (2, 1) carries marker 1 + 1 * 3 + 2.
  EXPECT_EQ(target.pixels[(static_cast<size_t>(kTileSize) + 0) * 4], 6);
  EXPECT_EQ(target.pixels[(static_cast<size_t>(kTileSize) + 0) * 4 + 3], 255);
  // The untouched column stays transparent.
  EXPECT_EQ(target.pixels[3], 0);
}

TEST(Blob47ComposeTest, CopyTileRejectsRegionsOutsideEitherImage) {
  constexpr int kTileSize = 8;
  RgbaImage source = Make3x3Atlas(kTileSize, 0, 0);

  RgbaImage target;
  target.width = kTileSize;
  target.height = kTileSize;
  target.pixels.assign(static_cast<size_t>(target.width) * target.height * 4, 0);

  EXPECT_EQ(CopyTile(source, 3 * kTileSize, 0, kTileSize, target, 0, 0).code(),
            absl::StatusCode::kOutOfRange);
  EXPECT_EQ(CopyTile(source, 0, 0, kTileSize, target, kTileSize, 0).code(),
            absl::StatusCode::kOutOfRange);
}

// --- TileShape identifiers ----------------------------------------------------

TEST(Blob47ComposeTest, TileShapeIdentifiersRoundTrip) {
  for (size_t i = 0; i < std::size(kTileShapeIdentifiers); ++i) {
    const std::optional<TileShape> shape = TileShapeFromIdentifier(kTileShapeIdentifiers[i]);
    ASSERT_TRUE(shape.has_value()) << kTileShapeIdentifiers[i];
    EXPECT_EQ(static_cast<size_t>(*shape), i);
  }
  EXPECT_FALSE(TileShapeFromIdentifier("kNotAShape").has_value());
  EXPECT_FALSE(TileShapeFromIdentifier("").has_value());
}

}  // namespace
}  // namespace zebes
