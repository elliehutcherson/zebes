#include "editor/level_editor/level_tiles.h"

#include "gtest/gtest.h"
#include "objects/level.h"

namespace zebes {
namespace {

// Places a tile without depending on the viewport's chunk-key encoding, which
// these queries do not care about.
TileChunk ChunkWithTiles(int count) {
  TileChunk chunk{};
  for (int i = 0; i < count; ++i) chunk.tiles[i] = i + 1;
  return chunk;
}

TEST(LevelTilesTest, AnEmptyLevelHasNoTiles) {
  Level level;

  EXPECT_FALSE(LevelHasTiles(level));
  EXPECT_EQ(CountPlacedTiles(level), 0);
}

// The editor allocates chunks eagerly, so an allocated chunk is not evidence
// that any tile ID exists. Counting chunks instead would freeze a level's
// tileset binding the moment it was first opened.
TEST(LevelTilesTest, AllocatedButEmptyChunksDoNotCount) {
  Level level;
  level.layers.front().tile_chunks[0] = TileChunk{};
  level.layers.front().tile_chunks[1] = TileChunk{};

  EXPECT_FALSE(LevelHasTiles(level));
  EXPECT_EQ(CountPlacedTiles(level), 0);
}

TEST(LevelTilesTest, OnePlacedTileCounts) {
  Level level;
  level.layers.front().tile_chunks[0] = ChunkWithTiles(1);

  EXPECT_TRUE(LevelHasTiles(level));
  EXPECT_EQ(CountPlacedTiles(level), 1);
}

TEST(LevelTilesTest, TilesAreCountedAcrossChunks) {
  Level level;
  level.layers.front().tile_chunks[0] = ChunkWithTiles(3);
  level.layers.front().tile_chunks[1] = TileChunk{};
  level.layers.push_back(WorldLayer{.id = 1, .name = "Foreground"});
  level.layers.back().tile_chunks[2] = ChunkWithTiles(2);

  EXPECT_TRUE(LevelHasTiles(level));
  EXPECT_EQ(CountPlacedTiles(level), 5);
}

}  // namespace
}  // namespace zebes
