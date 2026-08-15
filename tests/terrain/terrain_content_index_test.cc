#include "terrain/terrain_content_index.h"

#include <vector>

#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

// A 4x4 atlas of 2x2 cells, each cell filled with one value so a cell's
// identity is readable straight off the pixel data.
RgbaImage AtlasOfCells(const std::vector<uint8_t>& cell_values) {
  RgbaImage atlas;
  atlas.width = 4;
  atlas.height = 4;
  atlas.pixels.assign(4 * 4 * 4, 0);

  for (size_t cell = 0; cell < cell_values.size(); ++cell) {
    const int origin_x = static_cast<int>(cell % 2) * 2;
    const int origin_y = static_cast<int>(cell / 2) * 2;
    for (int y = 0; y < 2; ++y) {
      for (int x = 0; x < 2; ++x) {
        const size_t index = (static_cast<size_t>(origin_y + y) * 4 + origin_x + x) * 4;
        atlas.pixels[index + 0] = cell_values[cell];
        atlas.pixels[index + 1] = cell_values[cell];
        atlas.pixels[index + 2] = cell_values[cell];
        atlas.pixels[index + 3] = 255;
      }
    }
  }
  return atlas;
}

Tileset TilesetOfCells(int count) {
  Tileset tileset;
  tileset.name = "Test";
  tileset.tile_width = 2;
  tileset.tile_height = 2;
  for (int i = 0; i < count; ++i) {
    tileset.tiles.push_back(Tile{
        .id = i + 1,
        .name = absl::StrCat("Tile", i + 1),
        .source_x = (i % 2) * 2,
        .source_y = (i / 2) * 2,
        .shape = TileShape::kFullBlock,
    });
  }
  return tileset;
}

RgbaImage SolidCell(uint8_t value) {
  RgbaImage cell;
  cell.width = 2;
  cell.height = 2;
  for (int i = 0; i < 4; ++i) {
    cell.pixels.insert(cell.pixels.end(), {value, value, value, 255});
  }
  return cell;
}

TEST(TerrainContentIndexTest, FindsATileByTheArtworkItHolds) {
  const absl::StatusOr<TerrainContentIndex> index =
      TerrainContentIndex::Build(TilesetOfCells(4), AtlasOfCells({10, 20, 30, 40}));
  ASSERT_OK(index);

  EXPECT_EQ(index->Find(SolidCell(30)), 3);
  EXPECT_EQ(index->Find(SolidCell(99)), std::nullopt);
}

TEST(TerrainContentIndexTest, TilesDrawnIdenticallyCollapseOntoTheLowestId) {
  // Two cells with the same pixels. Which tile answers must not depend on the
  // order of the tile table, or a rebuild could hand a level a different ID for
  // artwork that never changed.
  const absl::StatusOr<TerrainContentIndex> index =
      TerrainContentIndex::Build(TilesetOfCells(4), AtlasOfCells({10, 20, 10, 40}));
  ASSERT_OK(index);

  EXPECT_EQ(index->Find(SolidCell(10)), 1);
  EXPECT_EQ(index->size(), 3);
}

TEST(TerrainContentIndexTest, InsertMakesANewlyAppendedTileFindable) {
  absl::StatusOr<TerrainContentIndex> index =
      TerrainContentIndex::Build(TilesetOfCells(2), AtlasOfCells({10, 20}));
  ASSERT_OK(index);
  ASSERT_EQ(index->Find(SolidCell(77)), std::nullopt);

  ASSERT_OK(index->Insert(SolidCell(77), /*tile_id=*/9));

  EXPECT_EQ(index->Find(SolidCell(77)), 9);
}

TEST(TerrainContentIndexTest, InsertingArtworkThatAlreadyExistsIsRefused) {
  // The caller skipped a Find that would have reused tile 1. Appending anyway
  // would grow the atlas with a duplicate, which is the exact thing content
  // addressing exists to prevent, so it fails rather than quietly accumulating.
  absl::StatusOr<TerrainContentIndex> index =
      TerrainContentIndex::Build(TilesetOfCells(2), AtlasOfCells({10, 20}));
  ASSERT_OK(index);

  const absl::Status status = index->Insert(SolidCell(10), /*tile_id=*/9);

  EXPECT_TRUE(absl::IsAlreadyExists(status)) << status;
}

TEST(TerrainContentIndexTest, ATileOutsideItsAtlasIsReportedNotSkipped) {
  Tileset tileset = TilesetOfCells(1);
  tileset.tiles.front().source_x = 900;

  const absl::StatusOr<TerrainContentIndex> index =
      TerrainContentIndex::Build(tileset, AtlasOfCells({10}));

  ASSERT_FALSE(index.ok());
  EXPECT_NE(index.status().message().find("not inside its atlas"), std::string::npos)
      << index.status();
}

TEST(TerrainContentIndexTest, ATilesetWithNoCellSizeIsRefused) {
  Tileset tileset = TilesetOfCells(1);
  tileset.tile_width = 0;

  EXPECT_FALSE(TerrainContentIndex::Build(tileset, AtlasOfCells({10})).ok());
}

TEST(TerrainContentIndexTest, CropTakesTheRegionAsked) {
  const RgbaImage atlas = AtlasOfCells({10, 20, 30, 40});

  const absl::StatusOr<RgbaImage> region = CropRegion(atlas, 2, 2, 2, 2);

  ASSERT_OK(region);
  EXPECT_EQ(region->pixels, SolidCell(40).pixels);
}

TEST(TerrainContentIndexTest, CropRefusesARegionLeavingTheImage) {
  const RgbaImage atlas = AtlasOfCells({10});

  EXPECT_FALSE(CropRegion(atlas, 3, 0, 2, 2).ok());
  EXPECT_FALSE(CropRegion(atlas, -1, 0, 2, 2).ok());
  EXPECT_FALSE(CropRegion(atlas, 0, 0, 0, 2).ok());
}

}  // namespace
}  // namespace zebes
