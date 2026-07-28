#include "editor/level_editor/viewport_model.h"

#include "gtest/gtest.h"
#include "objects/level.h"
#include "objects/tileset.h"

namespace zebes {
namespace {

TEST(LevelHasTilesTest, AnEmptyLevelHasNoTiles) {
  Level level;
  EXPECT_FALSE(LevelHasTiles(level));
}

// The editor allocates chunks eagerly, so an allocated chunk is not evidence
// that any tile ID exists.
TEST(LevelHasTilesTest, AllocatedButEmptyChunksDoNotCount) {
  Level level;
  level.tile_chunks[ChunkKey(0, 0)] = TileChunk{};
  level.tile_chunks[ChunkKey(1, 0)] = TileChunk{};

  EXPECT_FALSE(LevelHasTiles(level));
}

TEST(LevelHasTilesTest, OnePlacedTileCounts) {
  Level level;
  ASSERT_TRUE(SetTileAt(level, 3, 4, 7).ok());

  EXPECT_TRUE(LevelHasTiles(level));
}

TEST(ResolveTilesetBindingTest, NoPaletteSelectionLeavesTheBindingAlone) {
  Level level;
  level.tileset_id = "sunny";

  const TilesetBinding binding = ResolveTilesetBinding(level, nullptr);

  EXPECT_EQ(binding.tileset_id, "sunny");
  EXPECT_FALSE(binding.palette_matches);
}

TEST(ResolveTilesetBindingTest, AnUnboundLevelAdoptsThePaletteTileset) {
  Level level;
  const Tileset palette{.id = "grass"};

  const TilesetBinding binding = ResolveTilesetBinding(level, &palette);

  EXPECT_EQ(binding.tileset_id, "grass");
  EXPECT_TRUE(binding.palette_matches);
}

// Rebinding is safe while nothing is placed, which is what lets a level made
// against one tileset be pointed at another before any painting happens.
TEST(ResolveTilesetBindingTest, AnEmptyLevelRebindsToThePaletteTileset) {
  Level level;
  level.tileset_id = "sunny";
  const Tileset palette{.id = "grass"};

  const TilesetBinding binding = ResolveTilesetBinding(level, &palette);

  EXPECT_EQ(binding.tileset_id, "grass");
  EXPECT_TRUE(binding.palette_matches);
}

// The bug this guards: tile IDs painted under one tileset name different
// artwork under another, so a populated level keeps its binding.
TEST(ResolveTilesetBindingTest, APopulatedLevelKeepsItsTilesetAndRejectsTheSelection) {
  Level level;
  level.tileset_id = "sunny";
  ASSERT_TRUE(SetTileAt(level, 0, 0, 5).ok());
  const Tileset palette{.id = "grass"};

  const TilesetBinding binding = ResolveTilesetBinding(level, &palette);

  EXPECT_EQ(binding.tileset_id, "sunny");
  EXPECT_FALSE(binding.palette_matches);
}

TEST(ResolveTilesetBindingTest, APopulatedLevelAcceptsItsOwnTileset) {
  Level level;
  level.tileset_id = "sunny";
  ASSERT_TRUE(SetTileAt(level, 0, 0, 5).ok());
  const Tileset palette{.id = "sunny"};

  const TilesetBinding binding = ResolveTilesetBinding(level, &palette);

  EXPECT_EQ(binding.tileset_id, "sunny");
  EXPECT_TRUE(binding.palette_matches);
}

}  // namespace
}  // namespace zebes
