#include "editor/level_editor/viewport_model.h"

#include "gtest/gtest.h"
#include "macros.h"
#include "objects/level.h"
#include "objects/tileset.h"

namespace zebes {
namespace {

TEST(CalculateEntityBoundsTest, RenderOffsetMovesArtworkWithoutChangingEntityOrigin) {
  const Entity entity{.transform = {.position = {100, 200}}, .sprite_id = "sprite"};
  Sprite sprite{
      .frames = {SpriteFrame{.render_w = 32, .render_h = 64, .offset_x = -16, .offset_y = -56}},
  };

  absl::StatusOr<WorldRect> original = CalculateEntityBounds(entity, &sprite);
  ASSERT_OK(original);
  EXPECT_EQ(original->min, (Vec{84, 144}));
  EXPECT_EQ(original->max, (Vec{116, 208}));

  sprite.frames.front().offset_y = -52;
  absl::StatusOr<WorldRect> moved = CalculateEntityBounds(entity, &sprite);
  ASSERT_OK(moved);
  EXPECT_EQ(moved->min, (Vec{84, 148}));
  EXPECT_EQ(moved->max, (Vec{116, 212}));
  EXPECT_EQ(entity.transform.position, (Vec{100, 200}));
}

TEST(ResolvePaletteBindingTest, NoPaletteSelectionLeavesTheBindingAlone) {
  Level level;
  level.tileset_id = "sunny";

  const PaletteBinding binding = ResolvePaletteBinding(level, {});

  EXPECT_EQ(binding.tileset_id, "sunny");
  EXPECT_EQ(binding.tile, nullptr);
  EXPECT_FALSE(binding.terrain_id.has_value());
  EXPECT_EQ(binding.rejected_tileset, nullptr);
}

TEST(ResolvePaletteBindingTest, ANeverBoundLevelAdoptsThePaletteTileset) {
  Level level;
  const Tileset palette{.id = "grass"};
  const Tile tile{.id = 3};

  const PaletteBinding binding =
      ResolvePaletteBinding(level, {.tile_tileset = &palette, .tile = &tile});

  EXPECT_EQ(binding.tileset_id, "grass");
  EXPECT_EQ(binding.tile, &tile);
  EXPECT_EQ(binding.rejected_tileset, nullptr);
}

// Clicking a palette swatch must not repoint a level, even an empty one: the
// level's Tileset field is the only place that decision is made.
TEST(ResolvePaletteBindingTest, AnEmptyBoundLevelKeepsItsTileset) {
  Level level;
  level.tileset_id = "sunny";
  const Tileset palette{.id = "grass"};
  const Tile tile{.id = 3};

  const PaletteBinding binding =
      ResolvePaletteBinding(level, {.tile_tileset = &palette, .tile = &tile});

  EXPECT_EQ(binding.tileset_id, "sunny");
  EXPECT_EQ(binding.tile, nullptr);
  EXPECT_EQ(binding.rejected_tileset, &palette);
}

// The reported bug: a terrain from another tileset resolved its rules against
// the level's tileset, which does not have those tile IDs.
TEST(ResolvePaletteBindingTest, ATerrainFromAnotherTilesetIsRefused) {
  Level level;
  level.tileset_id = "sunny";
  ASSERT_OK(SetTileAt(level.layers.front(), 0, 0, 5));
  const Tileset palette{.id = "grass"};

  const PaletteBinding binding =
      ResolvePaletteBinding(level, {.terrain_tileset = &palette, .terrain_id = 1});

  EXPECT_EQ(binding.tileset_id, "sunny");
  EXPECT_FALSE(binding.terrain_id.has_value());
  EXPECT_EQ(binding.rejected_tileset, &palette);
}

TEST(ResolvePaletteBindingTest, ATerrainFromTheLevelsTilesetIsPaintable) {
  Level level;
  level.tileset_id = "grass";
  ASSERT_OK(SetTileAt(level.layers.front(), 0, 0, 5));
  const Tileset palette{.id = "grass"};

  const PaletteBinding binding =
      ResolvePaletteBinding(level, {.terrain_tileset = &palette, .terrain_id = 1});

  EXPECT_EQ(binding.tileset_id, "grass");
  ASSERT_TRUE(binding.terrain_id.has_value());
  EXPECT_EQ(*binding.terrain_id, 1);
  EXPECT_EQ(binding.rejected_tileset, nullptr);
}

TEST(ResolvePaletteBindingTest, ATileFromTheLevelsTilesetIsPaintable) {
  Level level;
  level.tileset_id = "sunny";
  const Tileset palette{.id = "sunny"};
  const Tile tile{.id = 9};

  const PaletteBinding binding =
      ResolvePaletteBinding(level, {.tile_tileset = &palette, .tile = &tile});

  EXPECT_EQ(binding.tile, &tile);
  EXPECT_EQ(binding.rejected_tileset, nullptr);
}

}  // namespace
}  // namespace zebes
