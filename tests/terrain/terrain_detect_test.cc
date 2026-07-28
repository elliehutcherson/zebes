#include "terrain/terrain_detect.h"

#include <set>

#include "gtest/gtest.h"
#include "terrain/blob47_compose.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

constexpr int kTileSize = 32;

// A minimal but valid quadrant sheet: every cell opaque, distinguishable only
// by variant. Composition detail is covered by blob47_compose_test.
QuadrantSheet MakeSheet(int variant_count) {
  QuadrantSheet sheet;
  sheet.quadrant_size = kTileSize / 2;
  sheet.variant_count = variant_count;
  sheet.image.width = sheet.quadrant_size * kQuadrantStateCount * variant_count;
  sheet.image.height = sheet.quadrant_size * kQuadrantCount;
  sheet.image.pixels.assign(
      static_cast<size_t>(sheet.image.width) * sheet.image.height * 4, 255);
  return sheet;
}

std::string ManifestFor(int variant_count) {
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(variant_count));
  EXPECT_TRUE(atlas.ok()) << atlas.status();
  return WriteBlob47Manifest(*atlas);
}

// Builds a tileset holding one or more stacked blob-47 blocks, matching the
// layout compose_blob47 emits.
Tileset MakeBlockTileset(int origin_column, int origin_row, int variant_count,
                         const std::string& name_prefix = "Grass") {
  Tileset tileset;
  tileset.name = "Generated";
  tileset.texture_id = "tx";
  tileset.tile_width = kTileSize;
  tileset.tile_height = kTileSize;

  int next_id = 1;
  for (int variant = 0; variant < variant_count; ++variant) {
    for (int i = 0; i < kBlob47TileCount; ++i) {
      const int column = origin_column + i % kBlob47Columns;
      const int row = origin_row + variant * kBlob47Rows + i / kBlob47Columns;
      tileset.tiles.push_back(Tile{
          .id = next_id,
          .name = absl::StrCat(name_prefix, "_", next_id),
          .source_x = column * kTileSize,
          .source_y = row * kTileSize,
      });
      ++next_id;
    }
  }
  return tileset;
}

// --- Manifest import (the primary path) --------------------------------------

TEST(TerrainDetectTest, ImportCreatesOneTilePerManifestCell) {
  absl::StatusOr<TerrainCandidate> candidate =
      ImportBlob47Manifest(ManifestFor(1), /*first_tile_id=*/1, /*terrain_id=*/1);
  ASSERT_TRUE(candidate.ok()) << candidate.status();

  EXPECT_EQ(candidate->tiles.size(), kBlob47TileCount);
  EXPECT_EQ(candidate->terrain.rules.size(), kBlob47TileCount);
  EXPECT_EQ(candidate->terrain.scheme, TerrainScheme::kBlob47);
}

TEST(TerrainDetectTest, ImportAssignsSequentialTileIdsFromTheGivenStart) {
  absl::StatusOr<TerrainCandidate> candidate =
      ImportBlob47Manifest(ManifestFor(1), /*first_tile_id=*/100, /*terrain_id=*/3);
  ASSERT_TRUE(candidate.ok()) << candidate.status();

  EXPECT_EQ(candidate->terrain.id, 3);
  for (int i = 0; i < kBlob47TileCount; ++i) {
    EXPECT_EQ(candidate->tiles[i].id, 100 + i);
  }
}

TEST(TerrainDetectTest, ImportCoversEveryMaskInTheTable) {
  absl::StatusOr<TerrainCandidate> candidate =
      ImportBlob47Manifest(ManifestFor(1), /*first_tile_id=*/1, /*terrain_id=*/1);
  ASSERT_TRUE(candidate.ok()) << candidate.status();

  std::set<uint8_t> rule_masks;
  for (const TerrainRule& rule : candidate->terrain.rules) {
    rule_masks.insert(rule.mask);
    EXPECT_EQ(rule.variants.size(), 1u) << "mask " << static_cast<int>(rule.mask);
  }

  for (uint8_t mask : Blob47MaskTable()) {
    EXPECT_EQ(rule_masks.count(mask), 1u) << "missing mask " << static_cast<int>(mask);
  }
}

// Multiple variants must collapse into one rule per mask, which is what gives
// the brush something to choose between.
TEST(TerrainDetectTest, ImportGroupsVariantsUnderOneRulePerMask) {
  absl::StatusOr<TerrainCandidate> candidate =
      ImportBlob47Manifest(ManifestFor(3), /*first_tile_id=*/1, /*terrain_id=*/1);
  ASSERT_TRUE(candidate.ok()) << candidate.status();

  EXPECT_EQ(candidate->tiles.size(), static_cast<size_t>(kBlob47TileCount) * 3);
  ASSERT_EQ(candidate->terrain.rules.size(), kBlob47TileCount);
  for (const TerrainRule& rule : candidate->terrain.rules) {
    EXPECT_EQ(rule.variants.size(), 3u) << "mask " << static_cast<int>(rule.mask);
  }
}

TEST(TerrainDetectTest, ImportTilesPointAtTheManifestSourceRects) {
  const std::string manifest = ManifestFor(1);
  absl::StatusOr<TerrainCandidate> candidate =
      ImportBlob47Manifest(manifest, /*first_tile_id=*/1, /*terrain_id=*/1);
  ASSERT_TRUE(candidate.ok()) << candidate.status();

  // Index 0 sits at the atlas origin; index 8 begins the second row.
  EXPECT_EQ(candidate->tiles[0].source_x, 0);
  EXPECT_EQ(candidate->tiles[0].source_y, 0);
  EXPECT_EQ(candidate->tiles[kBlob47Columns].source_x, 0);
  EXPECT_EQ(candidate->tiles[kBlob47Columns].source_y, kTileSize);
}

TEST(TerrainDetectTest, ImportRejectsMalformedJson) {
  absl::StatusOr<TerrainCandidate> candidate = ImportBlob47Manifest("{not json", 1, 1);
  ASSERT_FALSE(candidate.ok());
  EXPECT_EQ(candidate.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(TerrainDetectTest, ImportRejectsUnknownScheme) {
  absl::StatusOr<TerrainCandidate> candidate =
      ImportBlob47Manifest(R"({"scheme":"wang16","tiles":[]})", 1, 1);
  ASSERT_FALSE(candidate.ok());
  EXPECT_NE(candidate.status().message().find("wang16"), std::string::npos)
      << candidate.status().message();
}

TEST(TerrainDetectTest, ImportRejectsIncompleteCoverage) {
  absl::StatusOr<TerrainCandidate> candidate = ImportBlob47Manifest(
      R"({"scheme":"blob47","tiles":[{"mask":0,"variant":0,"source_x":0,"source_y":0}]})", 1, 1);
  ASSERT_FALSE(candidate.ok());
  EXPECT_EQ(candidate.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(TerrainDetectTest, ImportRejectsNonNormalizedMask) {
  // Mask 2 is a lone north-east diagonal, which normalization always clears.
  absl::StatusOr<TerrainCandidate> candidate = ImportBlob47Manifest(
      R"({"scheme":"blob47","tiles":[{"mask":2,"variant":0,"source_x":0,"source_y":0}]})", 1, 1);
  ASSERT_FALSE(candidate.ok());
  EXPECT_NE(candidate.status().message().find("non-normalized"), std::string::npos)
      << candidate.status().message();
}

TEST(TerrainDetectTest, ImportRejectsNonPositiveFirstTileId) {
  absl::StatusOr<TerrainCandidate> candidate = ImportBlob47Manifest(ManifestFor(1), 0, 1);
  ASSERT_FALSE(candidate.ok());
  EXPECT_EQ(candidate.status().code(), absl::StatusCode::kInvalidArgument);
}

// --- Slope units arrive as terrain members ------------------------------------

// A slope sheet with the two 45° floor units drawn.
SlopeSheet MakeSlopeSheet() {
  SlopeSheet sheet;
  sheet.tile_size = kTileSize;
  sheet.image.width = kTileSize * kSlopeShapeCount;
  sheet.image.height = kTileSize;
  sheet.image.pixels.assign(
      static_cast<size_t>(sheet.image.width) * sheet.image.height * 4, 0);

  for (int column : {0, 1}) {
    for (int y = 0; y < kTileSize; ++y) {
      for (int x = 0; x < kTileSize; ++x) {
        const size_t pixel =
            (static_cast<size_t>(y) * sheet.image.width + column * kTileSize + x) * 4;
        sheet.image.pixels[pixel + 3] = 255;
      }
    }
  }
  return sheet;
}

std::string ManifestWithSlopes() {
  SlopeSheet slopes = MakeSlopeSheet();
  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(MakeSheet(1), &slopes);
  EXPECT_TRUE(atlas.ok()) << atlas.status();
  return WriteBlob47Manifest(*atlas);
}

// This is where the slope pipeline and the seam fix meet: an imported slope is
// a terrain member from the moment it lands, so no manual assignment is needed.
TEST(TerrainDetectTest, ImportRegistersSlopesAsTerrainMembers) {
  absl::StatusOr<TerrainCandidate> candidate =
      ImportBlob47Manifest(ManifestWithSlopes(), /*first_tile_id=*/1, /*terrain_id=*/1);
  ASSERT_TRUE(candidate.ok()) << candidate.status();

  EXPECT_EQ(candidate->tiles.size(), kBlob47TileCount + 2);
  ASSERT_EQ(candidate->terrain.member_tile_ids.size(), 2u);

  // Members are the trailing tiles, and they are not painted by any rule.
  for (int member_id : candidate->terrain.member_tile_ids) {
    for (const TerrainRule& rule : candidate->terrain.rules) {
      for (const TerrainVariant& variant : rule.variants) {
        EXPECT_NE(variant.tile_id, member_id) << "a slope must never be a paint target";
      }
    }
  }
}

TEST(TerrainDetectTest, ImportedSlopesCarryTheirTileShape) {
  absl::StatusOr<TerrainCandidate> candidate =
      ImportBlob47Manifest(ManifestWithSlopes(), /*first_tile_id=*/1, /*terrain_id=*/1);
  ASSERT_TRUE(candidate.ok()) << candidate.status();

  std::set<TileShape> shapes;
  for (int member_id : candidate->terrain.member_tile_ids) {
    for (const Tile& tile : candidate->tiles) {
      if (tile.id == member_id) shapes.insert(tile.shape);
    }
  }
  EXPECT_EQ(shapes, (std::set<TileShape>{TileShape::kSlope45BottomLeft,
                                         TileShape::kSlope45BottomRight}));
}

TEST(TerrainDetectTest, ImportWithoutSlopesLeavesMembersEmpty) {
  absl::StatusOr<TerrainCandidate> candidate =
      ImportBlob47Manifest(ManifestFor(1), /*first_tile_id=*/1, /*terrain_id=*/1);
  ASSERT_TRUE(candidate.ok()) << candidate.status();
  EXPECT_TRUE(candidate->terrain.member_tile_ids.empty());
}

TEST(TerrainDetectTest, ImportRejectsNonSlopeShapeInSlopesArray) {
  // kFullBlock is not a slope and must not be smuggled in as a member.
  absl::StatusOr<TerrainCandidate> candidate = ImportBlob47Manifest(
      R"({"scheme":"blob47","tiles":[],"slopes":[{"shape":1,"source_x":0,"source_y":0}]})", 1, 1);
  ASSERT_FALSE(candidate.ok());
  EXPECT_EQ(candidate.status().code(), absl::StatusCode::kInvalidArgument);
}

// --- Atlas layout scan (the manifest-less fallback) ---------------------------

TEST(TerrainDetectTest, DetectFindsABlockAtTheAtlasOrigin) {
  absl::StatusOr<std::vector<TerrainCandidate>> candidates =
      DetectBlob47Terrains(MakeBlockTileset(0, 0, 1));
  ASSERT_TRUE(candidates.ok()) << candidates.status();
  ASSERT_EQ(candidates->size(), 1u);

  const TerrainCandidate& candidate = (*candidates)[0];
  EXPECT_TRUE(candidate.tiles.empty()) << "detected tiles already exist";
  ASSERT_EQ(candidate.terrain.rules.size(), kBlob47TileCount);
  EXPECT_EQ(candidate.terrain.rules[0].mask, Blob47MaskTable()[0]);
}

TEST(TerrainDetectTest, DetectFindsABlockAtAnOffsetOrigin) {
  absl::StatusOr<std::vector<TerrainCandidate>> candidates =
      DetectBlob47Terrains(MakeBlockTileset(3, 5, 1));
  ASSERT_TRUE(candidates.ok()) << candidates.status();
  ASSERT_EQ(candidates->size(), 1u);
  EXPECT_EQ((*candidates)[0].terrain.rules.size(), kBlob47TileCount);
}

TEST(TerrainDetectTest, DetectAssignsMasksInTableOrder) {
  Tileset tileset = MakeBlockTileset(0, 0, 1);
  absl::StatusOr<std::vector<TerrainCandidate>> candidates = DetectBlob47Terrains(tileset);
  ASSERT_TRUE(candidates.ok()) << candidates.status();
  ASSERT_EQ(candidates->size(), 1u);

  const TerrainCandidate& candidate = (*candidates)[0];
  for (int i = 0; i < kBlob47TileCount; ++i) {
    EXPECT_EQ(candidate.terrain.rules[i].mask, Blob47MaskTable()[i]) << "at index " << i;
    ASSERT_EQ(candidate.terrain.rules[i].variants.size(), 1u);
    // Tiles were created in row-major order starting at ID 1.
    EXPECT_EQ(candidate.terrain.rules[i].variants[0].tile_id, i + 1);
  }
}

TEST(TerrainDetectTest, DetectMergesStackedBlocksAsVariants) {
  absl::StatusOr<std::vector<TerrainCandidate>> candidates =
      DetectBlob47Terrains(MakeBlockTileset(0, 0, 3));
  ASSERT_TRUE(candidates.ok()) << candidates.status();
  ASSERT_EQ(candidates->size(), 1u) << "stacked blocks are one terrain, not three";

  const TerrainCandidate& candidate = (*candidates)[0];
  ASSERT_EQ(candidate.terrain.rules.size(), kBlob47TileCount);
  for (const TerrainRule& rule : candidate.terrain.rules) {
    EXPECT_EQ(rule.variants.size(), 3u) << "mask " << static_cast<int>(rule.mask);
  }
}

TEST(TerrainDetectTest, DetectRejectsABlockWithAHole) {
  Tileset tileset = MakeBlockTileset(0, 0, 1);
  tileset.tiles.erase(tileset.tiles.begin() + 20);

  absl::StatusOr<std::vector<TerrainCandidate>> candidates = DetectBlob47Terrains(tileset);
  ASSERT_TRUE(candidates.ok()) << candidates.status();
  EXPECT_TRUE(candidates->empty());
}

// A hand-authored sheet of slopes and one-off pieces must not produce a false
// positive; SMW_CUTER is exactly this shape.
TEST(TerrainDetectTest, DetectFindsNothingInAHandAuthoredTileset) {
  Tileset tileset;
  tileset.name = "SMW_CUTER";
  tileset.texture_id = "tx";
  tileset.tile_width = kTileSize;
  tileset.tile_height = kTileSize;

  // A 3x3 grass block plus a scattering of slope pieces.
  int next_id = 1;
  for (int row = 1; row <= 3; ++row) {
    for (int column = 1; column <= 3; ++column) {
      tileset.tiles.push_back(Tile{.id = next_id++,
                                   .name = "Grass",
                                   .source_x = column * kTileSize,
                                   .source_y = row * kTileSize});
    }
  }
  for (int i = 0; i < 12; ++i) {
    tileset.tiles.push_back(Tile{.id = next_id++,
                                 .name = "Slope",
                                 .source_x = (5 + i) * kTileSize,
                                 .source_y = (2 + i % 3) * kTileSize});
  }

  absl::StatusOr<std::vector<TerrainCandidate>> candidates = DetectBlob47Terrains(tileset);
  ASSERT_TRUE(candidates.ok()) << candidates.status();
  EXPECT_TRUE(candidates->empty());
}

TEST(TerrainDetectTest, DetectSuggestsNameFromSharedTilePrefix) {
  absl::StatusOr<std::vector<TerrainCandidate>> candidates =
      DetectBlob47Terrains(MakeBlockTileset(0, 0, 1, "MossyStone"));
  ASSERT_TRUE(candidates.ok()) << candidates.status();
  ASSERT_EQ(candidates->size(), 1u);
  EXPECT_EQ((*candidates)[0].suggested_name, "MossyStone");
  EXPECT_EQ((*candidates)[0].terrain.name, "MossyStone");
}

TEST(TerrainDetectTest, DetectIgnoresTilesThatAreNotCellAligned) {
  Tileset tileset = MakeBlockTileset(0, 0, 1);
  // Nudge one tile off the grid; its cell then has no tile.
  tileset.tiles[10].source_x += 3;

  absl::StatusOr<std::vector<TerrainCandidate>> candidates = DetectBlob47Terrains(tileset);
  ASSERT_TRUE(candidates.ok()) << candidates.status();
  EXPECT_TRUE(candidates->empty());
}

TEST(TerrainDetectTest, DetectRejectsInvalidTileDimensions) {
  Tileset tileset = MakeBlockTileset(0, 0, 1);
  tileset.tile_width = 0;

  absl::StatusOr<std::vector<TerrainCandidate>> candidates = DetectBlob47Terrains(tileset);
  ASSERT_FALSE(candidates.ok());
  EXPECT_EQ(candidates.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
